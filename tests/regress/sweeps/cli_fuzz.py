"""Fuzz the CLI: each command form with odd argument values (empty, 0, -1, 1e400, abc, nan, unknown refs).
Prints CRASH / HANG for the call that killed or stuck the server; restarts it and goes on. ~4 min.
Usage: python3 tests/regress/sweeps/cli_fuzz.py   (no CRASH/HANG line = clean)"""
import sys, itertools, subprocess, time
sys.path.insert(0, __import__("os").path.join(__import__("os").path.dirname(__import__("os").path.abspath(__file__)), ".."))
import harness
from harness import Headless, find_binary, SimError, SimHang
h = Headless(find_binary()); sim = h.__enter__()
def build():
    sim.cli("circuit","new","--discard")
    sim.cli("comp","add","Fixed Voltage","--label","V","--at","-200,0","Voltage=5","Out=true")
    sim.cli("comp","add","Ground","--label","G","--at","-200,100")
    sim.cli("comp","add","Resistor","--label","R","--at","-100,0","Resistance=1k")
    sim.cli("comp","add","Capacitor","--label","C","--at","0,0","Capacitance=1u")
    sim.cli("comp","add","Oscope","--label","S","--at","100,0")
    sim.cli("comp","add","I2CMaster","--label","M","--at","-200,200")
    sim.cli("wire","add","V.outnod","R.lPin"); sim.cli("wire","add","R.rPin","C.lPin"); sim.cli("wire","add","C.rPin","G.Gnd")
build()
bad = ["", "-1", "0", "1e400", "abc", "nan", "99999999999", "-5ms", "0.5", "1e-30s", "R", "R.nope", "Z9", "R.lPin R.lPin"]
cmds = [
 ["sim","average","--for","{}","{}"],["sim","average","--for","1ms","{}"],["sim","run","{}"],["sim","run","--for","{}"],["sim","speed","{}"],
 ["scope","data","S","--last","{}"],["scope","data","S","--ch","{}"],["scope","data","S","--max","{}"],
 ["scope","set","S","--timediv","{}"],["scope","set","S","--voltdiv","{}"],["scope","set","S","--triglevel","{}"],["scope","set","S","--ch","{}","--voltdiv","1"],
 ["probe","{}"],["read","{}"],["status","{}"],["log","--since","{}"],["log","--last","{}"],
 ["comp","move","R","{}"],["comp","rotate","R","{}"],["comp","add","Resistor","--at","{}"],["comp","add","{}"],["comp","remove","{}"],
 ["comp","get","{}"],["comp","set","R","{}"],["comp","set","R","Resistance={}"],["comp","set","C","Capacitance={}"],["comp","link","{}","{}"],
 ["wire","add","{}","{}"],["wire","remove","{}"],["wire","current","{}"],["ui","dial","{}","{}"],["ui","set","{}","on"],["ui","click","{}"],["ui","press","{}"],
 ["settings","set","{}","{}"],["capture","circuit","--out","{}"],["select","{}"],["view","{}"],["undo","{}"],["redo","{}"],["circuit","description","--file","{}"],
 ["i2c","{}","0x50"],["i2c","M","{}"],["i2c","M","0x50","--write","{}"],["i2c","M","0x50","--read","{}"],["i2c","M","0x50","--timeout","{}"],
 ["circuit","open","{}"],["mcu","info","{}"],["mcu","mem","{}"],["serial","send","{}","{}"],["export","{}"],["circuit","fonts","{}"],
]
def alive():
    try: sim.cli("ping", timeout=10); return True
    except Exception: return False
n=0
for c in cmds:
    nph = c.count("{}")
    for combo in itertools.product(bad[:11] if nph>1 else bad, repeat=nph):
        it = iter(combo); args=[a if a!="{}" else next(it) for a in c]
        if args[:2]==["sim","run"] or args[:2]==["sim","speed"]: pass
        try: sim.cli(*args, timeout=20)
        except SimHang:
            print("HANG", args, flush=True); h.__exit__(None,None,None); sim=h.__enter__(); build(); continue
        except SimError: pass
        except Exception as e: print("EXC", args, e)
        if not alive():
            print("CRASH", args, flush=True); h.__exit__(None,None,None); sim=h.__enter__(); build()
        n+=1
print("done", n, "calls")
h.__exit__(None,None,None)
