#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN="$SCRIPT_DIR/../build/unifiedpass.so"

if [[ ! -f "$PLUGIN" ]]; then
  echo "ERROR: $PLUGIN not found. Run 'make' from the project root first."
  exit 1
fi

run_bench () {
  local src=$1
  local name="${src%.c}"
  local bc="/opt/${name}.bc"
  # local bc_pre="/opt/${name}-pre.bc"
  local bc_pre="${name}-pre.bc"
  local bc_post="${name}-post.bc"
  # local bc_post="/opt/${name}-post.bc"
  local log="${name}.log"

  printf '========================================================\n'
  printf 'Benchmark: %s\n' "$name"
  printf '========================================================\n'

  clang -fno-discard-value-names -Xclang -disable-O0-optnone -O0 -emit-llvm \
        -c "$SCRIPT_DIR/$src" -o "$bc"

  opt -bugpoint-enable-legacy-pm=1 -mem2reg "$bc" -o "$bc_pre"

  # opt -bugpoint-enable-legacy-pm=1 -load-pass-plugin="$PLUGIN" \
  #      -passes='flow-sensitive-points-to-analysis' \
  #      "$bc_pre" -o "$bc_post" > "$log" 2>&1

  opt -bugpoint-enable-legacy-pm=1 -load-pass-plugin="$PLUGIN" \
       -passes='flow-sensitive-points-to-analysis' \
       "$bc_pre" -o "$bc_post"

}

run_bench complex-pointer-types.c
# run_bench test.c