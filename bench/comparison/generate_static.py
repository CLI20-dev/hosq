#!/usr/bin/env python3
"""Reuse deterministic JSON bytes; independently digest each typed projection."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from generate import digest, MASK


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    subprocess.run([sys.executable, str(Path(__file__).with_name('generate.py')),
                    str(args.output)], check=True)
    specs = [('small', 'id', 0), ('small', 'all', 2),
             ('medium', 'id', 0), ('medium', 'many', 1), ('medium', 'all', 2),
             ('large', 'id', 0), ('large', 'all', 2),
             ('wide_first', 'id', 0), ('wide_last', 'id', 0),
             ('numbers', 'all', 3), ('strings_utf8', 'all', 4),
             ('strings_escaped', 'all', 4)]
    manifest, metadata = [], []
    for name, selection, mode in specs:
        filename = name + '.json'
        raw = (args.output / filename).read_bytes()
        wire = json.loads(raw)
        if mode == 0:
            selected = [{'id': row['id']} for row in wire]
            paths = ['id']
        elif mode == 1:
            fields = ('name', 'count', 'ratio', 'ok')
            selected = [{'id': row['id'], 'payload': {
                key: row['payload'][key] for key in fields}} for row in wire]
            paths = ['id'] + ['payload.' + key for key in fields]
        else:
            selected, paths = wire, ['*']
        expected = [0] * 10
        digest(selected, expected)
        expected = [value & MASK for value in expected]
        case = name + '/' + selection
        manifest.append(' '.join([case, filename, str(mode), *map(str, expected)]))
        metadata.append(dict(case=case, file=filename, mode=mode,
                             bytes=len(raw), rows=len(wire), selected_paths=paths,
                             sha256=hashlib.sha256(raw).hexdigest(), expected=expected))
    (args.output / 'manifest_static.txt').write_text('\n'.join(manifest) + '\n')
    (args.output / 'metadata_static.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(f'Generated {len(manifest)} typed/skip comparison cases')


if __name__ == '__main__':
    main()
