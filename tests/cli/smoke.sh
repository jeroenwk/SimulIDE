#!/usr/bin/env bash
# SimulIDE command line interface smoke test.
#
# Starts a headless SimulIDE on a private socket and drives it with
# "simulide cli": builds a circuit from scratch, measures it, presses a button,
# saves and reopens, runs Arduino and scripted-device examples, and checks
# serial I/O. Needs python3 (JSON checks).
#
# Usage: tests/cli/smoke.sh [path/to/simulide]
#        SIMULIDE=path/to/simulide tests/cli/smoke.sh

set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EXAMPLES="$ROOT/resources/examples"

BIN=${1:-${SIMULIDE:-}}
if [ -z "$BIN" ]; then
    BIN=$(find "$ROOT/build_XX/executables" -type f \( -path '*simulide.app/Contents/MacOS/simulide' -o -name simulide \) 2>/dev/null | head -1)
fi
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    echo "simulide binary not found: build first or pass its path" >&2
    exit 2
fi

SOCK="simulide-test-$$"
TMP=$(mktemp -d)
PASS=0
FAIL=0

cli()  { "$BIN" cli --socket "$SOCK" "$@"; }
jcli() { "$BIN" cli --socket "$SOCK" --json "$@"; }

# Extract a value from a JSON reply: json '<python expression on r>'
json() { python3 -c "import json,sys; r=json.load(sys.stdin); print($1)"; }

ok()   { PASS=$((PASS+1)); echo "  ok    $1"; }
fail() { FAIL=$((FAIL+1)); echo "  FAIL  $1"; [ $# -gt 1 ] && echo "        $2"; }

# check "description" <python boolean expression on r> -- command...
check() {
    local desc=$1 expr=$2; shift 3
    local out
    out=$(jcli "$@" 2>&1)
    if echo "$out" | python3 -c "import json,sys; r=json.load(sys.stdin); sys.exit(0 if ($expr) else 1)" 2>/dev/null
    then ok "$desc"
    else fail "$desc" "$out"
    fi
}

cleanup() {
    kill "$PID" 2>/dev/null
    rm -rf "$TMP"
}

echo "SimulIDE: $BIN"
"$BIN" --headless --socket "$SOCK" > "$TMP/server.log" 2>&1 &
PID=$!
trap cleanup EXIT

for _ in $(seq 50); do cli ping >/dev/null 2>&1 && break; sleep 0.2; done
if ! cli ping >/dev/null 2>&1; then
    echo "headless SimulIDE did not start; log:" >&2
    cat "$TMP/server.log" >&2
    exit 2
fi

echo "Basics"
check "ping"              'r["ok"]'                                  -- ping
check "unknown command"   'not r["ok"] and "valid" in r["error"]'    -- comp frobnicate

echo "Build a circuit from scratch"
cli - > "$TMP/build.out" 2>&1 <<'EOF'
circuit new --discard
comp add Battery  --label B1 --at 0,0     Voltage=9
comp add Push     --label SW --at 40,-40
comp add Resistor --label R1 --at 120,-40 Resistance=470
comp add Led      --label D1 --at 200,0   --rot 90
comp add Ground   --label G  --at 0,80
wire add B1.lPin SW.pinP0
wire add SW.switch0pinN R1.lPin
wire add R1.rPin D1.lPin
wire add D1.rPin G.Gnd
wire add B1.rPin G.Gnd
EOF
if [ $? -eq 0 ]; then ok "build script"; else fail "build script" "$(cat "$TMP/build.out")"; fi
# The second wire to G.Gnd splits the first one with a junction node: 6 wires
check "5 components, 6 wires, 1 junction" 'r["result"]["components"]==5 and r["result"]["wires"]==6 and r["result"]["nodes"]==1' -- circuit info
check "bad property is rejected atomically" 'not r["ok"]' -- comp add Resistor Bogus=1
check "...and leaves nothing behind" 'r["result"]["components"]==5' -- circuit info

echo "Simulate and measure"
check "run 20ms"                'r["result"]["state"]=="paused"'          -- sim run --for 20ms
check "LED off (button open)"   'abs(r["result"]["current"]) < 1e-6'      -- read D1
check "press button"            'r["ok"]'                                  -- ui press SW
check "run 20ms"                'r["ok"]'                                  -- sim run --for 20ms
check "LED on (~14 mA)"         '0.010 < r["result"]["current"] < 0.020'   -- read D1
check "Ohm's law across R1"     'abs(r["result"]["voltage"]/r["result"]["current"]-470) < 470*0.01' -- read R1
check "probe pins"              'r["result"][0]["voltage"] > 8.9'          -- probe R1.lPin R1.rPin
check "release button"         'r["ok"]'                                  -- ui release SW
check "run 20ms"                'r["ok"]'                                  -- sim run --for 20ms
check "LED off again"           'abs(r["result"]["current"]) < 1e-6'      -- read D1

echo "Current flow"
cli ui press SW >/dev/null; cli sim run --for 20ms >/dev/null   # LED on again
check "wire current ~14 mA"        '0.010 < [x for x in r["result"] if x["from"]=="R1.rPin"][0]["current"] < 0.020' -- wire current
check "Kirchhoff at the junction"   'abs(sum(x["current"] for x in r["result"] if x["to"].startswith("Node-"))-sum(x["current"] for x in r["result"] if x["from"].startswith("Node-"))) < 1e-9' -- wire current
check "no flagged wires"            'all("flag" not in x for x in r["result"])' -- wire current
check "sign: battery out is +"      '[x for x in r["result"] if x["from"]=="B1.lPin"][0]["current"] > 0' -- wire current
check "pin current in comp get"     'any("current" in p for p in r["result"]["pins"])' -- comp get R1
FLOW_WAS=$(jcli settings get currentFlow | json 'str(r["result"]).lower()')   # User preference: restored below
check "flow on"                     'r["ok"]'                                  -- settings set currentFlow true
check "...reads back on"            'r["result"] is True'                      -- settings get currentFlow
check "...and off again"            'r["ok"]'                                  -- settings set currentFlow false
cli settings set currentFlow "$FLOW_WAS" >/dev/null
check "threshold accepts 100n"      'r["ok"]'                                  -- settings set currentMin 100n
check "...and reports it"           'r["result"]=="100 nA"'                    -- settings get currentMin
check "bad threshold rejected"      'not r["ok"]'                              -- settings set currentMin banana
cli settings set currentMin 1u >/dev/null
RANGE_WAS=$(jcli settings get currentRange | json 'r["result"]')
check "range accepts 3"             'r["ok"]'                                  -- settings set currentRange 3
check "...and reports it"           'r["result"]==3'                           -- settings get currentRange
check "range 9 rejected"            'not r["ok"]'                              -- settings set currentRange 9
cli settings set currentRange "$RANGE_WAS" >/dev/null

echo "Colors"
CANVAS_WAS=$(jcli settings get canvasColor | json 'r["result"]')
check "set canvas color"            'r["ok"]'                                  -- settings set canvasColor "#202830"
check "...reads back"               'r["result"]=="#202830"'                   -- settings get canvasColor
check "capture uses it"             'r["ok"]'                                  -- capture circuit --file "$TMP/dark.png"
check "bad color rejected"          'not r["ok"] and "invalid color" in r["error"]' -- settings set wireColor nonsense
check "default resets"              'r["ok"]'                                  -- settings set canvasColor default
check "...to the classic canvas"    'r["result"]=="#f0f0d2"'                   -- settings get canvasColor
cli settings set canvasColor "$CANVAS_WAS" >/dev/null
THEME_COLORS=$(for k in canvasColor wireColor lineColor textColor bodyColor flowColor nameColor valueColor animHighColor animLowColor outHighColor outLowColor inHighColor inLowColor openHighColor; do jcli settings get $k | json 'r["result"]'; done)
check "preset Dark"                 'r["ok"]'                                  -- settings set colorTheme Dark
check "...is reported"              'r["result"]=="Dark"'                      -- settings get colorTheme
check "...black canvas"             'r["result"]=="#000000"'                   -- settings get canvasColor
check "...value label color"        'r["result"]=="#c8c8c8"'                   -- settings get valueColor
check "...animate wire high"        'r["result"]=="#ff6060"'                   -- settings get animHighColor
cli settings set wireColor "#123456" >/dev/null
check "changing a color: Custom"    'r["result"]=="Custom"'                    -- settings get colorTheme
check "unknown preset rejected"     'not r["ok"] and "Classic" in r["error"]'  -- settings set colorTheme banana
set -- $THEME_COLORS; for k in canvasColor wireColor lineColor textColor bodyColor flowColor nameColor valueColor animHighColor animLowColor outHighColor outLowColor inHighColor inLowColor openHighColor; do cli settings set $k "$1" >/dev/null; shift; done

echo "Capture and selection"
check "capture circuit"         'r["result"]["width"] > 100 and __import__("os").path.getsize(r["result"]["file"]) > 1000' -- capture circuit --file "$TMP/cap.png"
check "capture window"          'r["ok"]'                                  -- capture window view --file "$TMP/win.png"
check "select parts"            '[c["label"] for c in r["result"]]==["R1","D1"] or sorted(c["label"] for c in r["result"])==["D1","R1"]' -- select R1 D1
check "view show"               'r["result"]["zoom"] > 0'                  -- view show D1
check "selection follows"       '[c["label"] for c in r["result"]]==["D1"]' -- select
check "clear selection"         'r["result"]==[]'                          -- select --none

echo "Edit, undo, redo"
check "set R1 = 1k"             'r["ok"]'                                  -- comp set R1 Resistance=1k
check "R1 is 1000 Ohm"          'any(p["name"]=="Resistance" and p["value"].startswith("1 k") for p in r["result"]["properties"])' -- comp get R1
check "undo"                    'r["ok"]'                                  -- undo
check "R1 back to 470"          'any(p["name"]=="Resistance" and p["value"].startswith("470") for p in r["result"]["properties"])' -- comp get R1
check "redo"                    'r["ok"]'                                  -- redo
check "R1 is 1k again"          'any(p["name"]=="Resistance" and p["value"].startswith("1 k") for p in r["result"]["properties"])' -- comp get R1

echo "Save and reopen"
check "save"                    'r["ok"]'                                  -- circuit save "$TMP/led.sim1"
check "new circuit"             'r["result"]["components"]==0'             -- circuit new
check "reopen"                  'r["result"]["components"]==5 and r["result"]["wires"]==6 and r["result"]["nodes"]==1' -- circuit open "$TMP/led.sim1"
check "R1 kept its value"       'any(p["name"]=="Resistance" and p["value"].startswith("1 k") for p in r["result"]["properties"])' -- comp get R1
echo "not xml" > "$TMP/bad.sim1"
check "invalid file rejected"   'not r["ok"] and "valid circuit" in r["error"]' -- circuit open "$TMP/bad.sim1" --discard

echo "Removing wires around a junction"
JW=$(jcli wire list | json '" ".join(w["uid"] for w in r["result"] if "Node-" in w["from"]+w["to"])')
if [ "$(echo $JW | wc -w)" -eq 3 ]; then ok "3 wires at the junction"; else fail "3 wires at the junction" "$JW"; fi
check "remove one: the other two merge" 'len(r["result"]["removed"])==3 and len(r["result"]["merged"])==1' -- wire remove ${JW%% *}
check "junction dissolved"      'r["result"]["wires"]==4 and r["result"]["nodes"]==0' -- circuit info
check "undo"                    'r["ok"]'                                  -- undo
check "junction back"           'r["result"]["wires"]==6 and r["result"]["nodes"]==1' -- circuit info
# shellcheck disable=SC2086
check "remove all three at once" 'r["result"].get("merged",[])==[]'             -- wire remove $JW
check "nothing left behind"     'r["result"]["wires"]==3 and r["result"]["nodes"]==0' -- circuit info
check "undo"                    'r["ok"]'                                  -- undo
check "all restored"            'r["result"]["wires"]==6 and r["result"]["nodes"]==1' -- circuit info

echo "Pin positions, explicit junctions, layout notes"
check "comp add reports pin positions" 'r["result"]["pins"][0]["pos"]==[384,-200] and r["result"]["pins"][0]["side"]=="left"' \
      -- comp add Resistor --label RA --at 400,-200
check "comp get lists them too" 'any(p["id"]=="rPin" and p["pos"]==[416,-200] and p["side"]=="right" for p in r["result"]["pins"])' -- comp get RA
check "rotated: lPin on top"    'r["result"]["pins"][0]["pos"]==[480,-216] and r["result"]["pins"][0]["side"]=="up"' \
      -- comp add Resistor --label RB --at 480,-200 --rot 90
check "wire routes are orthogonal" 'r["result"]["points"]==[416,-200,480,-200,480,-184]' -- wire add RA.rPin RB.rPin
check "junction at a chosen point" 'r["result"]["points"][-2:]==[448,-200]' -- wire add RB.lPin @448,-200 --via 448,-216
check "...is a real junction"   'any(n.startswith("Node-") for w in r["result"] for n in (w["from"],w["to"]))' -- wire list
check "point off any wire"      'not r["ok"] and "no wire" in r["error"]'  -- wire add RA.lPin @0,-300
check "point at a wire end"     'not r["ok"] and "connect to that pin" in r["error"]' -- wire add RA.lPin @416,-200
check "overlap is flagged"      'r["ok"] and any("runs along" in m for m in r.get("messages",[]))' -- wire add RA.lPin @480,-192 --via 384,-184
check "pin pass-over is flagged" 'r["ok"] and any("passes over RA.rPin" in m for m in r.get("messages",[]))' -- wire add RA.lPin RB.lPin --via 384,-200
check "clean up"                'r["ok"]'                                  -- comp remove RA RB

echo "Properties and scope settings"
check "add wave generator"      'r["ok"]'                                  -- comp add WaveGen --label AC --at -200,0
check "choices listed as options" 'any(p["name"]=="Wave_Type" and "Sine" in p["options"] and "unit" not in p for p in r["result"]["properties"])' -- comp get AC
check "no unit-prefix marker"   'r["result"]["Phase"]=="90 º"'             -- comp set AC Phase=90
check "add scope"               'r["ok"]'                                  -- comp add Oscope --label S --at 200,-160
check "hidden props not listed" 'not any(p["name"]=="TimDiv" for p in r["result"]["properties"])' -- comp get S
check "...but listed with --all" 'any(p["name"]=="TimDiv" and p.get("hidden") for p in r["result"]["properties"])' -- comp get S --all
check "scope set"               'r["result"]["timeDiv"]=="5 ms" and r["result"]["trigger"]==0 and all(c["voltDiv"]==5 for c in r["result"]["channels"])' \
      -- scope set S --timediv 5ms --voltdiv 5 --trigger 0
check "scope set one channel"   '[c["voltDiv"] for c in r["result"]["channels"]]==[5,2,5,5]' -- scope set S --ch 1 --voltdiv 2
check "bad value rejected"      'not r["ok"]'                              -- scope set S --timediv 1s --trigger 9
check "...and nothing changed"  'r["result"]["timeDiv"]=="5 ms" and r["result"]["trigger"]==0' -- scope get S

echo "Capacitor charge display"
cli - > "$TMP/rc.out" 2>&1 <<'EOF'
circuit new --discard
comp add Rail        --label V1 --at 0,0 --rot 0 Voltage=10
comp add Resistor    --label R1 --at 64,0 Resistance=1k
comp add Capacitor   --label C1 --at 112,32 --rot 90 Capacitance=100u
comp add Ground      --label G  --at 112,96
wire add V1.outnod R1.lPin
wire add R1.rPin C1.lPin --via 112,0
wire add C1.rPin G.Gnd
EOF
if [ $? -eq 0 ]; then ok "RC build script"; else fail "RC build script" "$(cat "$TMP/rc.out")"; fi
CHARGE_WAS=$(jcli settings get capCharge | json 'str(r["result"]).lower()')
LABEL_WAS=$(jcli settings get capLabel | json 'r["result"]')
check "display on"                  'r["ok"]'                                  -- settings set capCharge true
check "label both"                  'r["ok"]'                                  -- settings set capLabel both
check "...reads back"               'r["result"]=="both"'                      -- settings get capLabel
check "bad label mode rejected"     'not r["ok"]'                              -- settings set capLabel loud
check "run to t = tau (100 ms)"     'r["ok"]'                                  -- sim run --for 100ms
check "vref 10 V from the source"   'r["result"]["vref"]==10 and r["result"]["vref_source"]=="sources"' -- read C1
check "level 63 % at tau"           'abs(r["result"]["level"]-0.632) < 0.01'   -- read C1
check "voltage matches level"       'abs(r["result"]["voltage"]-10*r["result"]["level"]) < 1e-6' -- read C1
check "capture with gauge"          'r["ok"]'                                  -- capture circuit C1 --scale 6 --file "$TMP/gauge.png"
check "run to 5 tau"                'r["ok"]'                                  -- sim run --for 400ms
check "level 99 % at 5 tau"         'abs(r["result"]["level"]-0.993) < 0.01'   -- read C1
check "override scale 40 V"         'r["ok"]'                                  -- settings set chargeScale 40
check "...used as vref"             'r["result"]["vref"]==40 and r["result"]["vref_source"]=="override" and abs(r["result"]["level"]-0.25) < 0.01' -- read C1
check "negative scale rejected"     'not r["ok"]'                              -- settings set chargeScale -3
check "save with the scale"         'r["ok"]'                                  -- circuit save "$TMP/rc.sim1"
check "auto again"                  'r["ok"]'                                  -- settings set chargeScale 0
check "reopen"                      'r["ok"]'                                  -- circuit open "$TMP/rc.sim1" --discard
check "...scale saved in circuit"   'r["result"]==40'                          -- settings get chargeScale
check "rating 5 V"                  'r["ok"]'                                  -- comp set C1 MaxVolt=5
check "run"                         'r["ok"]'                                  -- sim run --for 500ms
check "...over-voltage flagged"     '"overvoltage" in r["result"].get("flag","") and r["result"]["rating"]==5' -- read C1
cli - > "$TMP/peak.out" 2>&1 <<'EOF'
circuit new --discard
comp add Capacitor   --label C2 --at 0,0 --rot 90 Capacitance=10u InitVolt=5
comp add Resistor    --label R2 --at 48,0 --rot 90 Resistance=1M
comp add Ground      --label G  --at 0,64
wire add C2.lPin R2.lPin --via 0,-32 48,-32
wire add C2.rPin G.Gnd
wire add R2.rPin @0,32 --via 48,32
EOF
if [ $? -eq 0 ]; then ok "no-source build script"; else fail "no-source build script" "$(cat "$TMP/peak.out")"; fi
check "run"                         'r["ok"]'                                  -- sim run --for 20ms
check "no source: peak fallback"    'r["result"]["vref_source"]=="peak" and abs(r["result"]["vref"]-5) < 0.05' -- read C2
cli - > "$TMP/rev.out" 2>&1 <<'EOF'
circuit new --discard
comp add Rail        --label V1 --at 0,0 --rot 0 Voltage=5
comp add Resistor    --label R1 --at 64,0 Resistance=1k
comp add elCapacitor --label C3 --at 112,32 --rot 270 Capacitance=10u
comp add Ground      --label G  --at 112,96
wire add V1.outnod R1.lPin
wire add R1.rPin C3.rPin --via 112,0
wire add C3.lPin G.Gnd
EOF
if [ $? -eq 0 ]; then ok "reversed electrolytic build script"; else fail "reversed electrolytic build script" "$(cat "$TMP/rev.out")"; fi
check "run"                         'r["ok"]'                                  -- sim run --for 50ms
check "reversed flagged"            '"reversed" in r["result"].get("flag","") and r["result"]["voltage"] < 0' -- read C3
cli settings set capCharge "$CHARGE_WAS" >/dev/null
cli settings set capLabel "$LABEL_WAS" >/dev/null

echo "Arduino and serial"
ECHO="$EXAMPLES/Micro/Arduino/arduino_serial_echo"
check "open example (terminal window closed headless)" 'r["ok"] and any("dialog" in m for m in r.get("messages",[]))' \
      -- circuit open "$ECHO/arduino_serial_echo.sim1" --discard
check "load firmware"           'r["result"]["device"]=="mega328"'         -- mcu load Uno-1 "$ECHO/arduino_serial_echo.hex"
check "run 200ms"               'r["ok"]'                                  -- sim run --for 200ms
check "boot message"            'r["result"]["text"]=="Serial echo Test\n"' -- serial read Uno-1
check "send to MCU"             'r["result"]["queued"]==6'                 -- serial send Uno-1 hello --lf
check "run 50ms"                'r["ok"]'                                  -- sim run --for 50ms
check "MCU echoes"              'r["result"]["text"]=="hello\n"'           -- serial read Uno-1
check "terminal saw it all"     'r["result"]["text"]=="Serial echo Test\nhello\n"' -- serial read SerialTerm-2
check "read a register"         'r["result"]["addr"]==0xc1'                -- mcu mem Uno-1 UCSR0B

echo "Scripted device (AngelScript)"
SPI="$EXAMPLES/Scripted/SPI_Digipot"
check "open SPI digipot example" 'r["ok"]'                                 -- circuit open "$SPI/SPIDigipot-test.sim1" --discard
check "run 300ms"               'r["result"]["state"]=="paused"'           -- sim run --for 300ms
check "scope captured signal"   'max(p[1] for p in r["result"]["channels"][0]["samples"]) > 0.1' -- scope data Oscope-11

echo "Quit"
check "quit"                    'r["ok"]'                                  -- quit --discard
wait "$PID"
CODE=$?
if [ $CODE -eq 0 ]; then ok "server exited cleanly"; else fail "server exited cleanly" "exit code $CODE"; fi

echo
echo "passed: $PASS  failed: $FAIL"
[ "$FAIL" -eq 0 ]
