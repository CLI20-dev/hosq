#!/usr/bin/env python3
"""Compile matcher misuse cases independently, with runnable positive controls."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import sys
import tempfile


REJECTED = {
    1: "root decode_into", 2: "root decode_document_into",
    3: "Decoder::decode", 4: "Decoder::decode_document",
    5: "Decoder::decode_nested", 6: "fixed tuple element",
    7: "fixed constant-key value", 8: "fixed dynamic key",
    9: "typed Array", 10: "typed Mapping key", 11: "typed Mapping value",
    13: "typed Optional", 14: "root matching collect array",
    15: "root fixed matching collect array", 16: "root collect matching object key",
    17: "root collect matching object value", 18: "root fixed collect matching object key",
    19: "root fixed collect matching object value", 20: "construct MatchResult<int>",
    21: "construct MatchResult<void>", 22: "implicit MatchResult bool conversion",
    23: "default construct MatchResult", 24: "raw bool Elem terminal",
    25: "root matching each", 26: "root fixed matching each",
    28: "non-bool predicate result",
    29: "collect bool payload after matching", 30: "fixed tuple matching each",
    31: "root object each matching key", 32: "root object each matching value",
    33: "reader-specific root matching result",
    34: "reader-specific FastReader EntryInput matching result",
    35: "reader-specific typed Array matching result",
    36: "reader-specific collect matching result",
    37: "construct MatchResult failure", 38: "default construct MatchResult<void>",
    39: "presence Optional field without Try",
    40: "presence Optional field mixed with Try field",
    41: "raw bool Elem terminal with storage",
    42: "reader-specific presence Optional field without Try",
    43: "Entry aggregate value containing ordinary matching field",
    44: "Entry aggregate key containing ordinary matching field",
    45: "Entry array aggregate containing ordinary matching element",
    46: "Entry aggregate with reader-specific matching field",
    47: "Try Entry cannot enable matching inside ordinary nested fields",
    48: "Entry aggregate outside the existing schema contract",
}
ACCEPTED = {
    100: "Try candidate dispatch", 101: "root validation",
    102: "collect bool data", 103: "Try matching each",
    104: "ordinary reader-specific pipeline and FastReader EntryInput",
    105: "matched false bool payload", 106: "stored ordinary bool",
    107: "presence Optional field without matching",
    108: "Try constant-key matching field",
    109: "Entry aggregate value",
    110: "Entry aggregate containing real Try dispatch",
    111: "Entry array aggregate",
    112: "Entry aggregate with reader-specific ordinary pipeline",
    113: "Try matching collect array",
    114: "Try fixed matching collect array",
    115: "Try collect matching object key",
    116: "Try fixed collect matching object key and value",
    117: "Try nested matching collect",
    118: "Try collect matched false bool payload",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--compiler-id", required=True)
    parser.add_argument("--standard", required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--include", type=Path, required=True)
    parser.add_argument("--fast-float", choices=("0", "1"), required=True)
    args = parser.parse_args()
    msvc = args.compiler_id == "MSVC"

    with tempfile.TemporaryDirectory(prefix="hosq-match-boundaries-") as temp:
        scratch = Path(temp)

        def compile_case(case, run):
            output = scratch / f"case-{case}{'.exe' if msvc else ''}"
            if msvc:
                command = [args.compiler, "/nologo", f"/std:{args.standard}",
                           "/EHsc", f"/I{args.include}",
                           f"/DHOSQ_USE_FAST_FLOAT={args.fast_float}",
                           f"/DHOSQ_MATCH_CASE={case}", str(args.source)]
                command += [f"/Fe:{output}", f"/Fo:{output}.obj"] if run else ["/Zs"]
            else:
                command = [args.compiler, f"-std={args.standard}",
                           f"-I{args.include}", f"-DHOSQ_USE_FAST_FLOAT={args.fast_float}",
                           f"-DHOSQ_MATCH_CASE={case}", str(args.source)]
                command += ["-o", str(output)] if run else ["-fsyntax-only"]
            result = subprocess.run(command, cwd=scratch, capture_output=True, text=True)
            if run and result.returncode == 0:
                result = subprocess.run([str(output)], capture_output=True, text=True)
            return case, result

        # Run controls first. Shared syntax/header errors must fail the test,
        # instead of counting as successful rejection of invalid programs.
        for case, name in ACCEPTED.items():
            _, result = compile_case(case, True)
            if result.returncode:
                print(f"FAIL positive control: {name}\n{result.stdout}{result.stderr}",
                      file=sys.stderr)
                return 1
        failures = []
        with ThreadPoolExecutor(max_workers=2) as pool:
            for case, result in pool.map(lambda n: compile_case(n, False), REJECTED):
                if result.returncode == 0:
                    failures.append(f"incorrectly accepted: {REJECTED[case]} (case {case})")
        if failures:
            print("\n".join(failures), file=sys.stderr)
            return 1
    print(f"{args.standard}: {len(REJECTED)} matcher misuse cases rejected; "
          f"{len(ACCEPTED)} positive controls compiled and ran")
    return 0


if __name__ == "__main__":
    sys.exit(main())
