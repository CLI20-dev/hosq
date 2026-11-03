#!/usr/bin/env python3
"""Compile the fixed schema workload against a chosen hosq include directory."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--include', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('flags', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    flags = args.flags[1:] if args.flags[:1] == ['--'] else args.flags
    source = Path(__file__).resolve().with_name('decode_schema.cc')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    obj = output / 'decode_schema.o'
    # A syntax-only run must not report an object left by an earlier run.
    if obj.exists():
        obj.unlink()
    command = [args.compiler, '-std=c++20', *flags,
               '-I' + str(args.include.resolve()), '-c', str(source), '-o', str(obj)]
    version = subprocess.check_output([args.compiler, '--version'], text=True)
    with (output / 'compiler.log').open('w') as log:
        start = time.perf_counter()
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        elapsed = time.perf_counter() - start
    report = {
        'compiler': version.splitlines()[0],
        'platform': platform.platform(),
        'command': command,
        'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'headers_sha256': {
            name: hashlib.sha256((args.include / 'hosq' / name).read_bytes()).hexdigest()
            for name in ('decoder.hh', 'meta_match.hh')
        },
        'elapsed_seconds': elapsed,
        'object_bytes': obj.stat().st_size if obj.exists() else None,
        'returncode': result.returncode,
    }
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
