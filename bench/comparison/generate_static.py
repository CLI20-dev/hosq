#!/usr/bin/env python3
"""Reuse deterministic JSON bytes; independently digest each typed projection."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys

from generate import digest, MASK


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    subprocess.run([sys.executable, str(Path(__file__).with_name('generate.py')),
                    str(args.output)], check=True)
    # The shuffled cases are intentional controls: all three engines use their
    # unordered object path, so key order is varied without silently turning
    # the primary comparison into an ordered-vs-unordered comparison.  The
    # *_skip cases retain unknown payload members; *_known cases contain only
    # the five selected fields, separating selection work from skip work.
    specs = [('small', 'small.json', 'id', 0, 'canonical'),
             ('small', 'small.json', 'all', 2, 'canonical'),
             ('medium', 'medium.json', 'id', 0, 'canonical'),
             ('medium/many_skip', 'medium.json', 'many', 1, 'canonical'),
             ('medium', 'medium.json', 'all', 2, 'canonical'),
             ('medium/many_skip_shuffled', 'medium_shuffled.json', 'many', 1,
              'shuffled'),
             ('medium/all_shuffled', 'medium_shuffled.json', 'all', 2,
              'shuffled'),
             ('medium/many_known', 'medium_known.json', 'many', 1,
              'canonical'),
             ('medium/many_known_shuffled', 'medium_known_shuffled.json',
              'many', 1, 'shuffled'),
             ('large', 'large.json', 'id', 0, 'canonical'),
             ('large', 'large.json', 'all', 2, 'canonical'),
             ('wide_first', 'wide_first.json', 'id', 0, 'canonical'),
             ('wide_last', 'wide_last.json', 'id', 0, 'canonical'),
             ('numbers', 'numbers.json', 'all', 3, 'canonical'),
             ('strings_utf8', 'strings_utf8.json', 'all', 4, 'canonical'),
             ('strings_escaped', 'strings_escaped.json', 'all', 4,
              'canonical')]
    manifest, metadata = [], []
    shuffle_seed = 2026090901
    medium_rows = json.loads((args.output / 'medium.json').read_bytes())

    def shuffle_rows(rows, seed):
        rng = random.Random(seed)
        shuffled = []
        for row in rows:
            payload = list(row['payload'].items())
            rng.shuffle(payload)
            outer = [('id', row['id']), ('payload', dict(payload))]
            rng.shuffle(outer)
            shuffled.append(dict(outer))
        return shuffled

    shuffled = shuffle_rows(medium_rows, shuffle_seed)
    known = [
        {'id': row['id'], 'payload': {
            key: row['payload'][key] for key in ('name', 'count', 'ratio', 'ok')}}
        for row in medium_rows
    ]
    known_shuffled = shuffle_rows(known, shuffle_seed)
    (args.output / 'medium_shuffled.json').write_bytes(
        json.dumps(shuffled, ensure_ascii=False, separators=(',', ':')).encode())
    (args.output / 'medium_known.json').write_bytes(
        json.dumps(known, ensure_ascii=False, separators=(',', ':')).encode())
    (args.output / 'medium_known_shuffled.json').write_bytes(
        json.dumps(known_shuffled, ensure_ascii=False,
                   separators=(',', ':')).encode())
    for name, filename, selection, mode, ordering in specs:
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
        case = name if '/' in name else name + '/' + selection
        manifest.append(' '.join([case, filename, str(mode), *map(str, expected)]))
        metadata.append(dict(case=case, file=filename, mode=mode,
                             bytes=len(raw), rows=len(wire), selected_paths=paths,
                             ordering=ordering, ordering_seed=(shuffle_seed
                                                               if ordering == 'shuffled'
                                                               else None),
                             selection=selection,
                             sha256=hashlib.sha256(raw).hexdigest(), expected=expected))
    (args.output / 'manifest_static.txt').write_text('\n'.join(manifest) + '\n')
    (args.output / 'metadata_static.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(f'Generated {len(manifest)} typed/skip comparison cases')


if __name__ == '__main__':
    main()
