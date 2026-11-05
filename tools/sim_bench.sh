#!/bin/bash
# Simulation benchmark: runs each circuit of tools/sim_bench_circuits.txt at full speed for its span of
# simulated time, on a private headless instance, and prints the wall-clock time (best of N runs).
# Also used by tools/pgo_build.sh to train the PGO profile (with LLVM_PROFILE_FILE set).
#
# usage: tools/sim_bench.sh [--bin PATH] [--runs N] [--scale F] [--list FILE]
#   --bin    SimulIDE binary (default: the one in build_XX/executables)
#   --runs   runs per circuit, the best one is reported (default 3)
#   --scale  multiply every simulated time by F, e.g. 0.3 for a shorter training run (default 1)
#   --list   circuit list (default tools/sim_bench_circuits.txt)
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=""; RUNS=3; SCALE=1; LIST="$ROOT/tools/sim_bench_circuits.txt"
while [ $# -gt 0 ]; do
    case $1 in
        --bin)   BIN=$2; shift 2;;
        --runs)  RUNS=$2; shift 2;;
        --scale) SCALE=$2; shift 2;;
        --list)  LIST=$2; shift 2;;
        *) echo "usage: $0 [--bin PATH] [--runs N] [--scale F] [--list FILE]"; exit 2;;
    esac
done
[ -n "$BIN" ] || BIN=$(find "$ROOT/build_XX/executables" -type f -name simulide -perm -u+x 2>/dev/null | head -1)
[ -x "$BIN" ] || { echo "[sim_bench] ERROR: no SimulIDE binary (build first, or pass --bin)"; exit 1; }

log(){ echo "[sim_bench] $*" >&2; }
now(){ python3 -c 'import time; print(time.time())'; }
SOCK="simbench-$$"
cli(){ "$BIN" cli --socket "$SOCK" "$@" < /dev/null; }

log "binary: $BIN"
log "list: $LIST, runs: $RUNS, scale: $SCALE, socket: $SOCK"
"$BIN" --headless --socket "$SOCK" >/dev/null 2>&1 &
SRV=$!
cleanup(){ cli quit --discard >/dev/null 2>&1 || true
           for _ in $(seq 1 40); do kill -0 $SRV 2>/dev/null || return 0; sleep 0.25; done
           log "WARNING: headless instance $SRV did not quit, killing it"; kill $SRV 2>/dev/null || true; }
trap cleanup EXIT
for i in $(seq 1 40); do cli ping >/dev/null 2>&1 && break; sleep 0.25; done
cli ping >/dev/null 2>&1 || { log "ERROR: headless instance did not start"; exit 1; }
log "headless instance $SRV ready"

printf "%-16s %10s %10s\n" circuit simulated "wall (s)"
grep -v '^\s*#' "$LIST" | grep -v '^\s*$' | while IFS='|' read -r name span path setup; do
    name=$(echo $name); span=$(echo $span); path=$(echo $path)
    num=${span%%[a-z]*}; unit=${span#$num}
    span=$(python3 -c "print('%g' % ($num*$SCALE))")$unit
    [ "${path:0:1}" = / ] || path="$ROOT/$path"
    best=""
    for r in $(seq 1 "$RUNS"); do
        log "$name run $r: open $path"
        cli circuit open "$path" --discard >/dev/null
        if [ -n "${setup// }" ]; then
            IFS=';' read -ra cmds <<< "$setup"
            for c in "${cmds[@]}"; do log "$name setup: $c"; read -ra words <<< "$c"; cli "${words[@]}" >/dev/null; done
        fi
        cli sim speed 1000000 >/dev/null
        t0=$(now)
        cli sim run --for "$span" >/dev/null
        t1=$(now)
        w=$(python3 -c "print('%.2f' % ($t1-$t0))")
        log "$name run $r: $span simulated in $w s ($(cli circuit info | grep matrix | sed 's/  */ /g'))"
        cli sim stop >/dev/null
        if [ -z "$best" ] || python3 -c "import sys; sys.exit(0 if $w < $best else 1)"; then best=$w; fi
    done
    printf "%-16s %10s %10s\n" "$name" "$span" "$best"
done
