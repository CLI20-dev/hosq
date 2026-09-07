#!/usr/bin/env python3
"""Run interleaved end-to-end comparisons and retain measurement context."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
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
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    executable = args.executable.resolve()
    output = args.output.resolve()

    def power():
        return subprocess.run(['pmset', '-g', 'batt'], capture_output=True, text=True).stdout if shutil.which('pmset') else 'unavailable'

    command = [str(executable), str(args.fixtures.resolve()),
               '--benchmark_enable_random_interleaving=true',
               f'--benchmark_repetitions={args.repetitions}',
               f'--benchmark_min_time={args.min_time}s',
               '--benchmark_out_format=json', f'--benchmark_out={output / "results.json"}']
    context = dict(command=command, platform=platform.platform(),
                   started=time.strftime('%Y-%m-%dT%H:%M:%S%z'), power_before=power(),
                   executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
    shutil.copyfile(args.fixtures / 'metadata.json', output / 'fixtures.json')
    with (output / 'run.log').open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    context['power_after'] = power()
    context['exit_code'] = result.returncode
    (output / 'context.json').write_text(json.dumps(context, indent=2) + '\n')
    result.check_returncode()
    rows = json.loads((output / 'results.json').read_text())['benchmarks']
    groups = {}
    units = {'ns': 1, 'us': 1000, 'ms': 1000000, 's': 1000000000}
    for row in rows:
        if row.get('run_type') != 'iteration':
            continue
        groups.setdefault(row['run_name'], []).append(row['cpu_time'] * units[row['time_unit']])
    lines = ['| Case | HOSQ µs | yyjson µs | HOSQ / yyjson | CV H / Y |',
             '|---|---:|---:|---:|---:|']
    for name in sorted(groups):
        if not (name.endswith('/HOSQ') or name.endswith('/HOSQtyped')):
            continue
        case, implementation = name.rsplit('/', 1)
        h, y = groups[name], groups[case + '/yyjson']
        hm, ym = statistics.median(h), statistics.median(y)
        cv = lambda x: statistics.stdev(x) / statistics.mean(x) if len(x) > 1 else 0
        lines.append(f'| {case}' + (' (static)' if implementation == 'HOSQtyped' else '') + f' | {hm / 1000:.3f} | {ym / 1000:.3f} | {hm / ym:.3f} | {cv(h):.1%} / {cv(y):.1%} |')
    report = '\n'.join(lines) + '\n'
    (output / 'summary.md').write_text(report)
    print(report)


if __name__ == '__main__':
    main()
