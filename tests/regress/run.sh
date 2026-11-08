#!/usr/bin/env bash
# Run the regression tests (tests/regress/test_*.py), each against its own headless SimulIDE.
#
# Usage: tests/regress/run.sh [pattern]      e.g. tests/regress/run.sh bh-003
#        SIMULIDE=path/to/simulide tests/regress/run.sh
# Exit code: 0 all pass, 1 a test failed, 2 a harness error.
set -u
DIR=$(cd "$(dirname "$0")" && pwd)
PAT=${1:-}
PASS=0; FAIL=0; ERR=0; FAILED=""
for t in "$DIR"/test_*.py; do
    [ -e "$t" ] || continue
    name=$(basename "$t")
    case "$name" in *"$PAT"*) ;; *) continue ;; esac
    echo "[regress-run] ==== $name"
    python3 "$t"; rc=$?
    case $rc in
        0) PASS=$((PASS+1)) ;;
        1) FAIL=$((FAIL+1)); FAILED="$FAILED $name" ;;
        *) ERR=$((ERR+1)); FAILED="$FAILED $name(harness)" ;;
    esac
done
echo "[regress-run] passed $PASS, failed $FAIL, harness errors $ERR${FAILED:+ :$FAILED}"
[ $ERR -gt 0 ] && exit 2
[ $FAIL -gt 0 ] && exit 1
exit 0
