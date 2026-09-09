#!/usr/bin/env python3
"""Fresh-process three-library measurements with optional pre-bridge control."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import shutil
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('fixtures', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--before-executable', type=Path)
    parser.add_argument('--repetitions', type=int, default=5)
    parser.add_argument('--min-time', type=float, default=0.1)
    parser.add_argument('--seed', type=int, default=20260908)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    fixtures = args.fixtures.resolve()
    metadata = json.loads((fixtures / 'metadata_static.json').read_text())
    rng = random.Random(args.seed)
    cases = metadata.copy()
    rng.shuffle(cases)
    jobs = []
    for row in cases:
        engines = ['HOSQ', 'Glaze', 'yyjson']
        if args.before_executable and row['case'] in ('medium/all', 'large/all'):
            engines.append('HOSQ-before')
        rng.shuffle(engines)
        jobs.extend((row['case'], engine) for engine in engines)

    def power():
        if not shutil.which('pmset'):
            return 'unavailable'
        return subprocess.check_output(['pmset', '-g', 'batt'], text=True)

    binaries = {'current': args.executable.resolve()}
    if args.before_executable:
        binaries['before'] = args.before_executable.resolve()
    context = dict(seed=args.seed, platform=platform.platform(),
                   started=time.strftime('%Y-%m-%dT%H:%M:%S%z'),
                   power_before=power(), jobs=[], binaries={
                       k: dict(path=str(v), sha256=hashlib.sha256(v.read_bytes()).hexdigest())
                       for k, v in binaries.items()})
    shutil.copy2(fixtures / 'metadata_static.json', out / 'fixtures.json')
    samples = []
    for index, (case, engine) in enumerate(jobs):
        actual_engine = 'HOSQ' if engine == 'HOSQ-before' else engine
        binary = binaries['before' if engine == 'HOSQ-before' else 'current']
        raw = out / f'part-{index:02}.json'
        command = [str(binary), str(fixtures), case, actual_engine,
                   f'--benchmark_repetitions={args.repetitions}',
                   f'--benchmark_min_time={args.min_time}s',
                   '--benchmark_out_format=json', f'--benchmark_out={raw}']
        with (out / 'run.log').open('a') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        context['jobs'].append(dict(case=case, engine=engine, command=command,
                                    exit_code=result.returncode))
        (out / 'context.json').write_text(json.dumps(context, indent=2) + '\n')
        result.check_returncode()
        data = json.loads(raw.read_text())
        context.setdefault('benchmark_context', data.get('context', {}))
        values = [r for r in data['benchmarks'] if r.get('run_type') == 'iteration']
        if len(values) != args.repetitions or any(r.get('error_occurred') or
                r['run_name'] != case + '/' + actual_engine for r in values):
            raise RuntimeError('Invalid benchmark result: ' + case + '/' + engine)
        units = {'ns': .001, 'us': 1., 'ms': 1000., 's': 1000000.}
        for r in values:
            samples.append(dict(case=case, engine=engine,
                                repetition=r['repetition_index'],
                                cpu_us=r['cpu_time'] * units[r['time_unit']],
                                real_us=r['real_time'] * units[r['time_unit']]))
        print(f'{index+1}/{len(jobs)} {case}/{engine}', flush=True)
    context['power_after'] = power()
    context['finished'] = time.strftime('%Y-%m-%dT%H:%M:%S%z')
    (out / 'context.json').write_text(json.dumps(context, indent=2) + '\n')
    (out / 'samples.json').write_text(json.dumps(samples, indent=2) + '\n')
    summary = []
    for case, engine in jobs:
        values = [r['cpu_us'] for r in samples if (r['case'], r['engine']) == (case, engine)]
        cv = (statistics.stdev(values) / statistics.mean(values)
              if len(values) > 1 else 0.0)
        summary.append(dict(case=case, engine=engine, median_us=statistics.median(values),
                            min_us=min(values), max_us=max(values),
                            cv=cv))
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
