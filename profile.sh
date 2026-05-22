#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: profile.sh <binary> [args...]" >&2
  exit 1
fi

binary="$1"; shift
profraw="$(mktemp /tmp/hosq_profile_XXXXXX.profraw)"
profdata="${binary}.profdata"

LLVM_PROFILE_FILE="$profraw" "$binary" "$@"
llvm-profdata merge -output="$profdata" "$profraw"
rm -f "$profraw"

echo "$profdata"
