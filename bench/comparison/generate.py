#!/usr/bin/env python3
"""Deterministic valid JSON with independently computed result digests."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

MASK = (1 << 64) - 1


def digest(value, out):
    if isinstance(value, dict):
        out[8] += 1
        for child in value.values():
            digest(child, out)
    elif isinstance(value, list):
        out[9] += 1
        for child in value:
            digest(child, out)
    else:
        out[0] += 1
        if isinstance(value, str):
            raw = value.encode('utf-8')
            out[3] += 1
            out[4] += len(raw)
            h = 14695981039346656037
            for byte in raw:
                h = ((h ^ byte) * 1099511628211) & MASK
            out[5] += h
        elif value is None:
            out[7] += 1
        elif isinstance(value, bool):
            out[6] += value
        elif isinstance(value, int):
            out[1] += value
        else:
            out[2] += struct.unpack('<Q', struct.pack('<d', value))[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest, metadata = [], []

    def save(name, data, modes=('all',), escape=False):
        raw = json.dumps(data, ensure_ascii=escape, separators=(',', ':')).encode()
        filename = name + '.json'
        (args.output / filename).write_bytes(raw)
        # Read back serialized data to make the oracle describe the actual wire input.
        wire = json.loads(raw)
        for mode in modes:
            out = [0] * 10
            if mode == 'sparse':
                for row in wire:
                    digest(row['id'], out)
            else:
                digest(wire, out)
            out = [x & MASK for x in out]
            case = name + '/' + mode
            manifest.append(' '.join([case, filename, str(int(mode == 'sparse')),
                                      *map(str, out)]))
            metadata.append(dict(case=case, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(), expected=out))

    def row(i):
        return dict(id=i, payload={'name': 'sample-record', 'count': i % 997,
                                  'ratio': 1.25, 'ok': True, 'none': None,
                                  'values': [1, 2, 3, 4]})

    for name, n in [('small', 1), ('medium', 2048), ('large', 262144)]:
        save(name, [row(i) for i in range(n)], ('sparse', 'all'))
    # Identical members; only selected-key position changes.
    wide = {f'id_extra_{i:03}': i for i in range(128)}
    save('wide_first', [dict(id=i, **wide) for i in range(128)], ('sparse',))
    save('wide_last', [dict(wide, id=i) for i in range(128)], ('sparse',))
    deep = 1
    for _ in range(48):
        deep = {'child': deep}
    save('deep', [dict(id=i, payload=deep) for i in range(2048)])
    save('numbers', [dict(id=i, payload=[i, -i, 1.25, -2.5e-12, 6.022e23,
                                       18446744073709551615]) for i in range(8192)])
    # Same decoded output, UTF-8 versus JSON unicode escape spelling.
    strings = [dict(id=i, payload=('abc日本語😀' * 256)) for i in range(128)]
    save('strings_utf8', strings)
    save('strings_escaped', strings, escape=True)
    (args.output / 'manifest.txt').write_text('\n'.join(manifest) + '\n')
    (args.output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(f'Generated {len(manifest)} cases')


if __name__ == '__main__':
    main()
