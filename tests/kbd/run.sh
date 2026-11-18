#!/usr/bin/env bash
# Run the keyboard input tests (tests/kbd/test_K*.py, specs/keyboard-input.md), each against
# its own headless SimulIDE, with the harness of tests/regress.
#
# Usage: tests/kbd/run.sh [pattern]      e.g. tests/kbd/run.sh K1
# Exit code: 0 all pass, 1 a test failed, 2 a harness error.
set -u
DIR=$(cd "$(dirname "$0")" && pwd)
PAT=${1:-}
PASS=0; FAIL=0; ERR=0; FAILED=""
for t in "$DIR"/test_*.py; do
    [ -e "$t" ] || continue
    name=$(basename "$t")
    case "$name" in *"$PAT"*) ;; *) continue ;; esac
    echo "[kbd-run] ==== $name"
    python3 "$t"; rc=$?
    case $rc in
        0) PASS=$((PASS+1)) ;;
        1) FAIL=$((FAIL+1)); FAILED="$FAILED $name" ;;
        *) ERR=$((ERR+1)); FAILED="$FAILED $name(harness)" ;;
    esac
done
echo "[kbd-run] passed $PASS, failed $FAIL, harness errors $ERR${FAILED:+ :$FAILED}"
[ $ERR -gt 0 ] && exit 2
[ $FAIL -gt 0 ] && exit 1
exit 0
