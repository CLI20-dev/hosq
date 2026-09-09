#!/usr/bin/env python3
"""Run interleaved end-to-end comparisons and retain measurement context."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import os
import random
import re
import shutil
import statistics
import subprocess
import time


def main():
    p = argparse.ArgumentParser()
    p.add_argument('executable', type=Path)
    p.add_argument('fixtures', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--repetitions', type=int, default=5)
    p.add_argument('--min-time', type=float, default=0.1)
    p.add_argument('--seed', type=int, default=20260907)
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    executable = args.executable.resolve()
    output = args.output.resolve()

    def power():
        return subprocess.run(['pmset', '-g', 'batt'], capture_output=True, text=True).stdout if shutil.which('pmset') else 'unavailable'

    metadata = json.loads((args.fixtures / 'metadata.json').read_text())
    rng = random.Random(args.seed)
    cases = [row['case'] for row in metadata]
    rng.shuffle(cases)
    jobs = []
    for case in cases:
        engines = ['HOSQ', 'yyjson', 'Glaze']
        rng.shuffle(engines)
        jobs.extend((case, engine) for engine in engines)
    context = dict(mode='fresh process per case/implementation', seed=args.seed,
                   platform=platform.platform(),
                   started=time.strftime('%Y-%m-%dT%H:%M:%S%z'), power_before=power(),
                   executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(), jobs=[])
    shutil.copyfile(args.fixtures / 'metadata.json', output / 'fixtures.json')
    rows = []
    for index, (case, engine) in enumerate(jobs):
        raw = output / f'part-{index:02}.json'
        command = [str(executable), str(args.fixtures.resolve()),
                   f'--benchmark_filter=^{re.escape(case + "/" + engine)}$',
                   f'--benchmark_repetitions={args.repetitions}',
                   f'--benchmark_min_time={args.min_time}s',
                   '--benchmark_out_format=json', f'--benchmark_out={raw}']
        child_env = dict(os.environ, HOSQ_COMPARE_CASE=case, HOSQ_COMPARE_ENGINE=engine)
        with (output / 'run.log').open('a') as log:
            result = subprocess.run(command, env=child_env, stdout=log, stderr=subprocess.STDOUT)
        context['jobs'].append(dict(case=case, engine=engine, command=command, exit_code=result.returncode))
        (output / 'context.json').write_text(json.dumps(context, indent=2) + '\n')
        result.check_returncode()
        part = json.loads(raw.read_text())
        context.setdefault('benchmark_context', part.get('context', {}))
        expected_name = case + '/' + engine
        iterations = [row for row in part['benchmarks'] if row.get('run_type') == 'iteration']
        if len(iterations) != args.repetitions or any(row['run_name'] != expected_name or row.get('error_occurred') for row in iterations):
            raise RuntimeError('Missing or invalid measurements: ' + expected_name)
        rows.extend(part['benchmarks'])
    context['power_after'] = power()
    context['exit_code'] = 0
    (output / 'context.json').write_text(json.dumps(context, indent=2) + '\n')
    (output / 'results.json').write_text(json.dumps(dict(benchmarks=rows), indent=2) + '\n')
    groups = {}
    units = {'ns': 1, 'us': 1000, 'ms': 1000000, 's': 1000000000}
    for row in rows:
        if row.get('run_type') != 'iteration':
            continue
        groups.setdefault(row['run_name'], []).append(row['cpu_time'] * units[row['time_unit']])
    lines = ['| Case | HOSQ µs | yyjson µs | Glaze µs | H / Y | G / Y |',
             '|---|---:|---:|---:|---:|---:|']
    dispersion = {}
    cases = [row['case'] for row in json.loads((args.fixtures / 'metadata.json').read_text())]
    for case in cases:
        names = [case + '/HOSQ', case + '/yyjson', case + '/Glaze']
        samples = [groups[name] for name in names]
        h, y, g = [statistics.median(x) for x in samples]
        for name, values in zip(names, samples):
            dispersion[name] = statistics.stdev(values) / statistics.mean(values) if len(values) > 1 else 0
        lines.append(f'| {case} | {h / 1000:.3f} | {y / 1000:.3f} | {g / 1000:.3f} | {h / y:.3f} | {g / y:.3f} |')
    (output / 'dispersion.json').write_text(json.dumps(dispersion, indent=2) + '\n')
    report = '\n'.join(lines) + '\n'
    (output / 'summary.md').write_text(report)
    print(report)


if __name__ == '__main__':
    main()
