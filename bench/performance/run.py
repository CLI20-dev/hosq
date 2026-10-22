#!/usr/bin/env python3
"""Build one fixed harness against two revisions and report ABBA measurements."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess

CASES = ('id_only', 'partial_fields', 'all_fields', 'all_fields_randomize',
         'id_only_pretty', 'partial_fields_pretty', 'all_fields_pretty',
         'all_fields_randomize_pretty')


def verification(text):
    pattern = r'(\w+) bytes=(\d+) fnv1a64=(\d+) checksum=([\d,]+)'
    rows = {}
    for line in text.splitlines():
        match = re.fullmatch(pattern, line)
        if not match or match[1] in rows:
            raise ValueError('Malformed or duplicate verification record')
        rows[match[1]] = {'bytes': int(match[2]), 'input_hash': match[3],
                          'checksum': match[4]}
    if set(rows) != set(CASES) or any(row['bytes'] <= 0 for row in rows.values()):
        raise ValueError('Verification must contain all eight nonempty inputs')
    return rows


def verify_equal(base, head):
    if base != head:
        raise ValueError('Base/head input bytes, hashes, or checksums differ')


def analyze(documents, name, repetitions):
    cpu, wall = [], []
    for document in documents:
        rows = [r for r in document['benchmarks'] if r.get('run_type', 'iteration') == 'iteration']
        if len(rows) != repetitions:
            raise ValueError('Unexpected repetition count')
        for row in rows:
            if row.get('error_occurred') or row.get('run_name', row['name']).split('/')[0] != name:
                raise ValueError('Benchmark failed or wrong case')
            factor = {'ns': 1, 'us': 1000, 'ms': 1000000, 's': 1000000000}.get(row['time_unit'])
            if factor is None:
                raise ValueError('Unknown timing unit')
            values = [row['cpu_time'] * factor, row['real_time'] * factor]
            if any(not math.isfinite(v) or v <= 0 for v in values):
                raise ValueError('Timing must be finite and positive')
            cpu.append(values[0]); wall.append(values[1])
    return {'cpu_ns': statistics.median(cpu), 'wall_ns': statistics.median(wall),
            'cv_percent': statistics.stdev(cpu) / statistics.mean(cpu) * 100 if len(cpu) > 1 else 0,
            'samples': len(cpu), 'processes': len(documents)}


def command(args, log):
    with log.open('w') as stream:
        result = subprocess.run([str(a) for a in args], stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f'Command failed ({result.returncode}); see {log}')
    return log.read_text()


def markdown(report):
    lines = ['# HOSQ PR performance', '', 'Report only. Positive change means slower; hosted-runner noise can dominate small differences.', '',
             '| Case | Base CPU ns | Head CPU ns | CPU change | Base wall ns | Head wall ns | CPU CV base/head |',
             '|---|---:|---:|---:|---:|---:|---:|']
    for row in report['cases']:
        b, h = row['base'], row['head']
        lines.append(f"| {row['name']} | {b['cpu_ns']:.1f} | {h['cpu_ns']:.1f} | {(h['cpu_ns']/b['cpu_ns']-1)*100:+.2f}% | {b['wall_ns']:.1f} | {h['wall_ns']:.1f} | {b['cv_percent']:.2f}% / {h['cv_percent']:.2f}% |")
    return '\n'.join(lines) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('base', 'head', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--repository', default=os.getenv('GITHUB_REPOSITORY'), required=not os.getenv('GITHUB_REPOSITORY'))
    p.add_argument('--base-sha', default=os.getenv('BASE_SHA'), required=not os.getenv('BASE_SHA'))
    p.add_argument('--head-sha', default=os.getenv('HEAD_SHA'), required=not os.getenv('HEAD_SHA'))
    p.add_argument('--run-id', type=int, default=os.getenv('GITHUB_RUN_ID'), required=not os.getenv('GITHUB_RUN_ID'))
    p.add_argument('--repetitions', type=int, default=5)
    p.add_argument('--min-time', type=float, default=0.15)
    p.add_argument('--warmup', type=float, default=0)
    a = p.parse_args()
    if any(not re.fullmatch('[0-9a-fA-F]{40}', sha) for sha in (a.base_sha, a.head_sha)) or a.run_id < 0:
        p.error('Require full SHA identities and nonnegative run ID')
    if a.repetitions < 1 or not math.isfinite(a.min_time) or a.min_time <= 0 or not math.isfinite(a.warmup) or a.warmup < 0:
        p.error('Require positive repetitions/min-time and nonnegative warmup')
    out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parent
    binaries, verified = {}, {}
    for label, checkout in (('base', a.base), ('head', a.head)):
        build = out / ('build-' + label)
        command(['cmake', '-S', source, '-B', build, '-DCMAKE_BUILD_TYPE=Release',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', f'-DHOSQ_INCLUDE_DIR={checkout.resolve() / "include"}'], out / f'{label}-configure.log')
        command(['cmake', '--build', build, '--parallel', '2', '--verbose'], out / f'{label}-build.log')
        binaries[label] = build / 'hosq_performance'
        verified[label] = verification(command([binaries[label], '--verify-only'], out / f'{label}-verify.log'))
    verify_equal(verified['base'], verified['head'])
    report = dict(schema_version=1, repository=a.repository, base_sha=a.base_sha, head_sha=a.head_sha, run_id=a.run_id,
                  protocol={'order': 'ABBA per workload; separate processes', 'summary': 'median CPU/wall; sample CPU CV',
                            'repetitions_per_process': str(a.repetitions), 'min_time_seconds': str(a.min_time), 'warmup_seconds': str(a.warmup)}, cases=[])
    for name in CASES:
        documents = {'base': [], 'head': []}
        for index, label in enumerate(('base', 'head', 'head', 'base')):
            raw = out / f'{name}-{index}-{label}.json'
            command([binaries[label], f'--benchmark_filter=^{name}$', f'--benchmark_repetitions={a.repetitions}',
                     f'--benchmark_min_time={a.min_time}s', f'--benchmark_min_warmup_time={a.warmup}',
                     '--benchmark_time_unit=ns', f'--benchmark_out={raw}', '--benchmark_out_format=json'],
                    out / f'{name}-{index}-{label}.log')
            documents[label].append(json.loads(raw.read_text()))
        row = {k: v for k, v in verified['base'][name].items() if k != 'checksum'}
        row.update(name=name, **{label: analyze(docs, name, a.repetitions) for label, docs in documents.items()})
        report['cases'].append(row)
    (out / 'report.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    (out / 'report.md').write_text(markdown(report))


if __name__ == '__main__':
    main()
