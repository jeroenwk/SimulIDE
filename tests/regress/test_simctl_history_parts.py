"""Going back in time with the other part types (specs/sim-control.md §4.8): state code for the
BJT, logic gates and buffers, flip-flops, counters, memory, the 555, relays, the transformer,
sources, meters, switches, displays, the noise generator, and the AVR and PIC MCUs (core,
interrupts, peripherals, ports).

For each example circuit: history usable (no class missing), then `sim history check`: run on
over three checkpoints, go back 4 ms to a keyframe, replay to the same checkpoints and compare
every node voltage and every element's saved state, bit for bit. Noise is checked once with
noise on (its random generators are part of the keyframe).
"""
import os
from harness import run_test

EX = os.path.join(os.path.dirname(__file__), "..", "..", "resources", "examples")
TC = os.path.join(os.path.dirname(__file__), "..", "circuits")
CIRCUITS = [
    ("BJT", EX + "/Active/BJT/Osc_Hartley.sim1"),
    ("flip-flops JK", EX + "/Logic/Memory/flipflopJK_test.sim1"),
    ("555 astable", EX + "/Logic/lm555-astable.sim1"),
    ("relay latch", EX + "/Switches/Relay/relay-Latch2.sim1"),
    ("transformer", TC + "/transformer_bandpass.sim1"),
    ("potentiometer, op amp", EX + "/Active/Diode/precision_rectifier.sim1"),
    ("8-bit CPU (memory, gates, buffers)", EX + "/Logic/8bit_cpu/8bit_16inst_CPU/8bit_CPU-B16.sim1"),
    ("analog mux", EX + "/Active/analog_mux.sim1"),
    ("shift register", EX + "/Logic/Arithmetic/shiftregister-test.sim1"),
    ("voltage regulator", EX + "/Active/Volt_Reg/voltreg-voltage.sim1"),
    ("SCR", EX + "/Active/SCR.sim1"),
    ("lamp", EX + "/Outputs/lamp.sim1"),
    ("LED matrix", EX + "/Outputs/Leds/led_matrix.sim1"),
    ("keypad", EX + "/Switches/keypad.sim1"),
    ("7490 counter, push", EX + "/Logic/7490_clock-2.sim1"),
    ("Arduino: ADC, timers, PCD8544 (port input bits)", EX + "/Micro/Arduino/oscope8544/oscope8544.sim1"),
    ("Arduino + DHT22 (pin callbacks switched on and off)", EX + "/Micro/Sensors/Arduino_DHT22/dht22_test.sim1"),
    ("mega32 USART", EX + "/Micro/Avr/mega32/m32_usart/mega32_usart.sim1"),
    ("tiny85 pin-change interrupt", EX + "/Micro/Avr/tiny85/tiny85_pcint/pcint_001.sim1"),
    ("PIC 12F675 timer 1", EX + "/Micro/Pic/12F675/12F675_T1-XC8/12F675_T1-XC8.sim1"),
    ("PIC 16F876 I2C LCD", EX + "/Micro/Pic/16F87x/16F876_I2C-Lcd/i2c-lcd.sim1"),
    ("Arduino + serial terminal (echo)", EX + "/Micro/Arduino/arduino_serial_echo/arduino_serial_echo.sim1"),
    ("Arduino + scripted I2C digipot (dial resynced)", EX + "/Scripted/I2C_Digipot/I2CDigipot_test.sim1"),
]


def roundtrip(sim, name, path, noise=False):
    c = sim.cli
    c("circuit", "open", os.path.abspath(path), "--discard", timeout=60)
    if noise:
        c("settings", "set", "noise", "true")
    c("sim", "start")
    h = c("sim", "history")
    sim.check("%s: usable" % name, 1 if h["usable"] else 0, 1, absolute=0, note=", ".join(h.get("missing", [])))
    for i in range(6):
        c("sim", "skip", "1ms", timeout=60)
    k = c("sim", "history", "check", "--span", "1ms", "--points", "3", "--ago", "4ms", timeout=300)
    sim.check("%s: identical after going back" % name, 1 if k["ok"] else 0, 1, absolute=0,
              note="%s of %s the same; differ %s" % (k["same"], len(k["points"]), k.get("differ", [])))
    c("sim", "stop")


def test(sim):
    sim.cli("settings", "set", "history", "true")
    sim.cli("settings", "set", "runSeed", "4321")
    for name, path in CIRCUITS:
        roundtrip(sim, name, path)
    roundtrip(sim, "BJT with noise", EX + "/Active/BJT/Osc_Hartley.sim1", noise=True)


if __name__ == "__main__":
    run_test(test)
