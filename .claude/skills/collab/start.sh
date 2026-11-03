#!/bin/sh
# Start (or reuse) the visible SimulIDE GUI for a collaboration session and
# print the current circuit state. Used by the /collab skill.
# Honors SIMULIDE_SOCKET (default socket: simulide-cli).

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
B=$(find "$ROOT/build_XX/executables" -type f -name simulide -perm -u+x 2>/dev/null | head -1)
if [ -z "$B" ]; then
    echo "STATUS: NOT BUILT. Build first (see CLAUDE.md: qmake && make in build_XX)."
    exit 2
fi
echo "B=$B"

if "$B" cli ping >/dev/null 2>&1; then
    echo "GUI: already running, reusing it"
else
    LOG="${TMPDIR:-/tmp}/simulide-gui.log"
    nohup "$B" > "$LOG" 2>&1 &
    i=0
    until "$B" cli ping >/dev/null 2>&1; do
        i=$((i+1))
        if [ $i -gt 40 ]; then echo "GUI: failed to start, see $LOG"; exit 1; fi
        sleep 0.25
    done
    "$B" cli view raise >/dev/null 2>&1
    echo "GUI: started"
fi

echo "--- circuit";    "$B" cli circuit info
echo "--- components"; "$B" cli comp list
echo "--- selection";  "$B" cli select
echo "--- simulation"; "$B" cli sim status
echo "--- capture";    "$B" cli capture circuit --file "${TMPDIR:-/tmp}/simulide-collab-start.png" | grep '^file'
