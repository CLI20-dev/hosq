#!/usr/bin/env python3
"""Compare writer fixtures with independent msgpack, PyMongo, tomllib and JSON.

Binary codecs check exact bytes as well as decoded values. TOML/JSON check
semantic types/values, including negative zero. Requires msgpack and pymongo;
use the documented temporary Nix environment instead of a global install.
"""
import argparse
import bson
from bson.int64 import Int64
import json
import math
import msgpack
from pathlib import Path
import platform
import pymongo
import struct
import subprocess
import sys
import tomllib


def equal(actual, expected):
    if isinstance(expected, dict):
        assert isinstance(actual, dict) and actual.keys() == expected.keys()
        for key in expected:
            equal(actual[key], expected[key])
    elif isinstance(expected, list):
        assert isinstance(actual, list) and len(actual) == len(expected)
        for a, b in zip(actual, expected):
            equal(a, b)
    elif isinstance(expected, bool):
        assert type(actual) is bool and actual == expected
    elif isinstance(expected, int):
        assert isinstance(actual, int) and not isinstance(actual, bool) and actual == expected
    elif isinstance(expected, float):
        assert isinstance(actual, float)
        if math.isnan(expected):
            assert math.isnan(actual)
        else:
            assert actual == expected
            if expected == 0:
                assert math.copysign(1, actual) == math.copysign(1, expected)
    else:
        assert type(actual) is type(expected) and actual == expected


def expectation(name, fmt):
    if name == "nested":
        return {"n": 42, "text": 'a\n"\\😀', "ok": True,
                "items": [{"n": 1}, {"n": 2}], "meta": {"n": -7},
                "edge": [-2**63, 2**63-1], "floats": [1.0, -0.0, 0.1, 1e300]}
    if name == "weird-key":
        return {'a.b\n"\\': 42}
    if name == "binary":
        return {"x": b"\0\xff"} if fmt == "bson" else b"\0\xff"
    kind, value = name.split(":", 1)
    if kind == "text":
        values = {"empty": "", "escapes": 'a\n"\\', "unicode": "日本語😀",
                  "controls": "".join(chr(n) for n in range(32)) + chr(127)}
        return {"x": values[value]}
    if kind == "str":
        return "a" * int(value)
    if kind == "array":
        return [0] * int(value)
    if kind == "ext":
        return msgpack.ExtType(7, b"x" * int(value))
    if kind in {"f32", "f64"}:
        number = float(value) if value not in {"denorm", "max"} else (
            float.fromhex("0x0.0000000000001p-1022") if value == "denorm" else sys.float_info.max)
        if kind == "f32":
            number = struct.unpack("<f", struct.pack("<f", number))[0]
    else:
        number = int(value)
        if fmt == "bson" and (kind in {"i64", "u64"} or (kind == "u32" and number > 2**31-1)):
            number = Int64(number)
    return {"x": number} if fmt in {"bson", "toml"} else number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("generator", type=Path)
    args = parser.parse_args()
    generated = subprocess.run([str(args.generator.resolve())], capture_output=True, text=True, check=True)
    counts = {}
    for line in generated.stdout.splitlines():
        fmt, name, hex_bytes = line.split("\t")
        data = bytes.fromhex(hex_bytes)
        expected = expectation(name, fmt)
        try:
            if fmt == "msgpack":
                encoded = msgpack.packb(expected, use_bin_type=True, use_single_float=name.startswith("f32:"))
                assert data == encoded, "bytes differ from msgpack"
                actual = msgpack.unpackb(data, raw=False, strict_map_key=False)
            elif fmt == "bson":
                assert data == bson.BSON.encode(expected), "bytes differ from PyMongo"
                actual = bson.BSON(data).decode()
            elif fmt == "toml":
                actual = tomllib.loads(data.decode("utf-8"))
            else:
                actual = json.loads(data)
            equal(actual, expected)
        except AssertionError as error:
            raise AssertionError(f"{fmt}/{name}: {error}") from error
        counts[fmt] = counts.get(fmt, 0) + 1
    print(f"Python {platform.python_version()}, msgpack {msgpack.__version__}, PyMongo {pymongo.__version__}")
    print(f"Independent writer checks: {sum(counts.values())} passed; {counts}")


if __name__ == "__main__":
    main()
