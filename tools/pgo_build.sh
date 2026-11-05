#!/bin/bash
# Profile-guided optimisation build (specs/pgo-build.md), macOS / Apple clang:
#   1. build an instrumented SimulIDE in build_pgo_gen/ (CONFIG+=pgo_gen)
#   2. train it: run the benchmark circuits (tools/sim_bench_circuits.txt) once, shortened
#   3. merge the .profraw files into pgo/simulide.profdata
#   4. rebuild build_XX from scratch: SimulIDE.pro uses the profile when it exists
# Afterwards, tools/sim_bench.sh measures the result. Rerun this script after larger source changes
# (a stale profile still works, but functions that changed lose the optimisation).
#
# usage: tools/pgo_build.sh [--scale F]   (training length, default 0.3 of the benchmark times)
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCALE=0.3
[ "${1:-}" = "--scale" ] && SCALE=$2
JOBS=$(sysctl -n hw.ncpu)
GEN="$ROOT/build_pgo_gen"
PGO="$ROOT/pgo"
export PATH=/opt/homebrew/opt/qt@5/bin:$PATH

log(){ echo "[pgo_build] $*"; }
fail(){ log "ERROR: $*"; exit 1; }

command -v qmake >/dev/null || fail "qmake not found (Homebrew qt@5)"
xcrun --find llvm-profdata >/dev/null 2>&1 || fail "llvm-profdata not found (Xcode command line tools)"
if pgrep -f "build_XX/executables/.*/simulide" >/dev/null; then
    fail "SimulIDE from build_XX is running: quit it first (step 4 replaces that binary)"
fi

log "1/4 instrumented build in $GEN (log: $GEN/make.log)"
mkdir -p "$GEN"
printf 'BUILD_DIR = $$PWD\ninclude(../SimulIDE.pro)\n' > "$GEN/SimulIDE_Build.pro"
( cd "$GEN" && qmake CONFIG+=pgo_gen > qmake.log 2>&1 && make -j"$JOBS" > make.log 2>&1 ) \
    || fail "instrumented build failed, see $GEN/make.log"
GENBIN=$(find "$GEN/executables" -type f -name simulide -perm -u+x | head -1)
log "instrumented binary: $GENBIN"

log "2/4 training run (scale $SCALE)"
rm -rf "$PGO/raw"; mkdir -p "$PGO/raw"
LLVM_PROFILE_FILE="$PGO/raw/%p.profraw" "$ROOT/tools/sim_bench.sh" --bin "$GENBIN" --runs 1 --scale "$SCALE"
n=$(ls "$PGO/raw"/*.profraw 2>/dev/null | wc -l | tr -d ' ')
[ "$n" -gt 0 ] || fail "no .profraw files written"
log "$n raw profiles in $PGO/raw"

log "3/4 merging into $PGO/simulide.profdata"
xcrun llvm-profdata merge -o "$PGO/simulide.profdata" "$PGO/raw"/*.profraw
rm -rf "$PGO/raw"
ls -l "$PGO/simulide.profdata" | sed 's/^/[pgo_build] /'

log "4/4 full rebuild of build_XX with the profile (log: $ROOT/build_XX/make.log)"
cd "$ROOT/build_XX"
qmake > qmake.log 2>&1 || fail "qmake failed, see $ROOT/build_XX/qmake.log"
sed -n 's/.*\(PGO: .*\)/[pgo_build] qmake: \1/p' qmake.log
make clean > /dev/null 2>&1 || true
make -j"$JOBS" > make.log 2>&1 || fail "PGO build failed, see $ROOT/build_XX/make.log"
grep -q "fprofile-instr-use" "$ROOT/build_XX/Makefile" || fail "build_XX/Makefile does not use the profile"
log "done: $(find "$ROOT/build_XX/executables" -type f -name simulide -perm -u+x | head -1)"
log "measure it with tools/sim_bench.sh"
