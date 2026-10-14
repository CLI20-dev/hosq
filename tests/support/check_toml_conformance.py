#!/usr/bin/env python3
"""Check the TOML 1.0 corpus against the companion C++ executable.

Checks every invalid input is rejected, every valid input is accepted, and
valid key/container/type structure plus string/integer/float/boolean values match
its golden JSON. Date/time values are intentionally excluded: HOSQ
has no calendar conversion schema. Strings use the owned read_string API.
No corpus is downloaded by this script; pass a pinned toml-lang/toml-test tree.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess


def normalized(value):
    if isinstance(value, list):
        return [normalized(item) for item in value]
    if isinstance(value, dict):
        if set(value) == {"type", "value"} and isinstance(value["type"], str):
            kind, data = value["type"], value["value"]
            if kind in {"date-local", "time-local", "datetime", "datetime-local"}:
                return kind, "raw"
            if kind == "integer":
                return kind, int(data)
            if kind == "float":
                number = float(data)
                return kind, "nan" if math.isnan(number) else number
            return kind, data
        return {key: normalized(item) for key, item in value.items()}
    raise ValueError(f"unexpected tagged JSON: {value!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("corpus", type=Path)
    args = parser.parse_args()
    tests = args.corpus.resolve() / "tests"
    files = (tests / "files-toml-1.0.0").read_text()
    result = subprocess.run([str(args.executable.resolve()), str(tests)],
                            input=files, capture_output=True, text=True, check=True)
    valid = invalid = 0
    failures = []
    for line in result.stdout.splitlines():
        actual = json.loads(line)
        path = actual["file"]
        expected_valid = path.startswith("valid/")
        if expected_valid:
            valid += 1
        else:
            invalid += 1
        if actual["ok"] != expected_valid:
            failures.append(f"{path}: acceptance differs: {actual}")
        elif expected_valid:
            expected = json.loads((tests / path).with_suffix(".json").read_text())
            if normalized(actual["tree"]) != normalized(expected):
                failures.append(f"{path}: key/type/numeric structure differs")
    expected_count = sum(line.endswith(".toml") for line in files.splitlines())
    if valid + invalid != expected_count:
        failures.append(f"case count differs: {valid + invalid} != {expected_count}")
    print(f"TOML 1.0.0: {valid} valid, {invalid} invalid; {len(failures)} failures")
    for failure in failures:
        print(failure)
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
