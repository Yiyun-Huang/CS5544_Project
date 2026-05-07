#!/bin/bash

# =============================================================================
# run_benchmarks.sh
#
# Runs both the flow-sensitive analysis (our solution) and the flow-insensitive
# Andersen baseline on every test program. Each benchmark gets two log files:
#   <name>.fs.log  -- flow-sensitive output
#   <name>.fi.log  -- flow-insensitive (baseline) output
#
# Pipeline per benchmark:
#   1. clang -O0 emits LLVM IR.
#   2. opt -mem2reg promotes alloca'd locals into SSA registers.
#   3. opt -load-pass-plugin=unifiedpass.so -passes=<analysis> runs the chosen
#      analysis and prints per-program-point points-to sets and metrics.
# =============================================================================

set -u

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
  local bc_pre="/opt/${name}-pre.bc"
  local bc_post_fs="/opt/${name}-fs-post.bc"
  local bc_post_fi="/opt/${name}-fi-post.bc"
  local log_fs="test_outputs/${name}.fs.log"
  local log_fi="test_outputs/${name}.fi.log"

  printf '\n========================================================\n'
  printf 'Benchmark: %s\n' "$name"
  printf '========================================================\n'

  clang -fno-discard-value-names -Xclang -disable-O0-optnone -O0 -emit-llvm \
        -c "$SCRIPT_DIR/$src" -o "$bc"

  opt -bugpoint-enable-legacy-pm=1 -mem2reg "$bc" -o "$bc_pre"

  printf '\n---- flow-sensitive ----\n'
  opt -bugpoint-enable-legacy-pm=1 -load-pass-plugin="$PLUGIN" \
       -passes='flow-sensitive-points-to-analysis' \
       "$bc_pre" -o "$bc_post_fs" 2>&1 | tee "$log_fs"

  printf '\n---- flow-insensitive (baseline) ----\n'
  opt -bugpoint-enable-legacy-pm=1 -load-pass-plugin="$PLUGIN" \
       -passes='flow-insensitive-points-to-analysis' \
       "$bc_pre" -o "$bc_post_fi" 2>&1 | tee "$log_fi"
}

# ---- micro-benchmarks ------------------------------------------------------
run_bench complex-pointer-types.c
run_bench iterative-example.c
run_bench test.c
run_bench test2.c

# ---- macro-benchmarks ------------------------------------------------------
run_bench linked-list.c
run_bench tree-ops.c
run_bench bt.c