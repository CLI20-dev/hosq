#!/usr/bin/env python3
"""Generate varied fixed-schema inputs for the static-gap diagnostic."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path


def load_comparison_generator():
    path = Path(__file__).resolve().parents[1] / "generate.py"
    spec = importlib.util.spec_from_file_location("hosq_comparison_generate", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load digest oracle from {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def varied_row(i):
    names = (
        "a",
        "long-ascii-name-0123456789",
        "日本語😀",
        "quote\" slash\\ line\nend",
    )
    counts = (0, 9_999_999, 10_000_000, (1 << 64) - 1)
    ratios = (1.25, -2.5e-12, 6.022e23, -0.0)
    values = (
        (0, 9, 10, (1 << 64) - 1),
        (99, 100, 9_999_999, 10_000_000),
        ((1 << 64) - 1, 0, 1, 42),
        (7, 8, 9, 10),
    )
    k = i % len(names)
    return {
        "id": i,
        "payload": {
            "name": names[k],
            "count": counts[k],
            "ratio": ratios[k],
            "ok": bool(k & 1),
            "none": None,
            "values": list(values[k]),
        },
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--pretty", action="store_true",
                        help="include indentation and JSON whitespace around colons")
    args = parser.parse_args()
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("output directory must be empty; use a separate diagnostic fixture directory")
    args.output.mkdir(parents=True, exist_ok=True)

    oracle = load_comparison_generator()
    data = [varied_row(i) for i in range(2048)]
    rows = [
        json.dumps(row, ensure_ascii=(i % 8 == 6), separators=(",", ":"))
        for i, row in enumerate(data)
    ]
    raw = ("[" + ",".join(rows) + "]").encode()
    if args.pretty:
        raw = json.dumps(data, ensure_ascii=False, indent=2,
                         separators=(",", " \t:\r ")).encode()
    wire = json.loads(raw)
    expected = [0] * 10
    oracle.digest(wire, expected)
    expected = [value & oracle.MASK for value in expected]

    stem = "medium_pretty" if args.pretty else "medium_varied"
    name = stem + "/all"
    filename = stem + ".json"
    (args.output / filename).write_bytes(raw)
    (args.output / "manifest.txt").write_text(
        " ".join((name, filename, "0", *map(str, expected))) + "\n"
    )
    metadata = [{
        "case": name,
        "bytes": len(raw),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "expected": expected,
    }]
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Generated {name} bytes={len(raw)}")


if __name__ == "__main__":
    main()
