"""Independent numerical references and shared checks for BH-053 / BH-055.

Backward Euler is dissipative, not lossless. With h as the reactive step and
d = 1-exp(-j*w*h), its sampled impedances are ZL=L*d/h and ZC=h/(C*d).
Near the continuous resonance their real parts sum to h/C. Thus the original
50mH / 120nF / 54ohm bandpass should have a gain near 0.8663 at h=1us,
not 1.0. At h=10ns it converges to the continuous circuit (gain 0.99845).

The delayed-read changes proposed in PRs #54 and #57 are not backward Euler:
they cancel the numerical damping in this example but introduce DC ringing;
#54 can also stop updating after an idle inductor sees another source step.
These tests check both frequency-domain accuracy and transient stability.
"""

import math
import os

CIRCUITS = os.path.join(os.path.dirname(__file__), '..', 'circuits')
L = 50e-3
C = 120e-9
R = 54.0


def open_bandpass(sim, transformer=False, step='1 us', solver_step='1 us'):
    fixture = 'transformer_bandpass.sim1' if transformer else 'reactive_bandpass.sim1'
    sim.cli('circuit', 'open', os.path.join(CIRCUITS, fixture), '--discard')
    # Reactive companions choose their step when stamped: configure before starting.
    sim.cli('settings', 'set', 'reactStep=' + step, 'stepSize=' + solver_step)


def gain_at(sim, frequency):
    sim.cli('comp', 'set', 'GEN', 'Freq=%s Hz' % frequency)
    sim.cli('sim', 'run', '--for', '40 ms')  # >20 amplitude-decay time constants
    # Simultaneous channels, whole periods: separate RMS windows bias the ratio.
    span = '%s ps' % round(20e12 / frequency)
    vi, vo = sim.cli('sim', 'average', '--for', span,
                    'GEN.outnod', 'R1.lPin')['channels']
    return vo['voltage']['rms'] / vi['voltage']['rms']


def bandpass_gain(frequency, step=None, series_r=0.0):
    w = 2 * math.pi * frequency
    if step is None:
        impedance = R + series_r + 1j * (w * L - 1 / (w * C))
    else:
        # 1-cos(theta) = 2*sin(theta/2)^2 avoids small-step cancellation.
        d = complex(2 * math.sin(w * step / 2) ** 2, math.sin(w * step))
        impedance = R + series_r + L * d / step + step / (C * d)
    return abs(R / impedance)


def frequency_response(sim, transformer=False):
    series_r = 0.002 if transformer else 2e-6  # eCoil's two 1mohm leads / L and C ESR
    resonance = []
    for step in (1e-6, 100e-9, 10e-9):
        open_bandpass(sim, transformer, '%s ns' % (step * 1e9))
        frequencies = (1969, 2055, 2141) if step != 100e-9 else (2055,)
        for frequency in frequencies:
            actual = gain_at(sim, frequency)
            expected = bandpass_gain(frequency, step, series_r)
            sim.check('BE bandpass %g Hz, h=%g ns' % (frequency, step * 1e9),
                      actual, expected, tol=0.003)
            if frequency == 2055:
                resonance.append(actual)
            if step == 10e-9:
                sim.check('fine step converges to continuous circuit, %g Hz' % frequency,
                          actual, bandpass_gain(frequency, series_r=series_r), tol=0.003)

    ideal = bandpass_gain(2055, series_r=series_r)
    errors = [abs(g - ideal) for g in resonance]
    sim.check('resonance error decreases at each smaller reactive step',
              errors[0] > errors[1] > errors[2], True, tol=0)

    # The source and reactive elements are event driven: changing only stepSize
    # must not be mistaken for lowering the companions' integration interval.
    open_bandpass(sim, transformer, '1 us', '10 ns')
    sim.check('smaller solver step alone leaves coarse reactive gain unchanged',
              gain_at(sim, 2055), resonance[0], tol=0.001)


def rl_highpass(sim, transformer=False):
    errors = []
    coil = 'T1' if transformer else 'L1'
    property_name = 'BaseInd' if transformer else 'Inductance'
    for step in ('1 us', '100 ns', '10 ns'):
        open_bandpass(sim, transformer, step)
        # A 1F coupling capacitor is negligible here (<0.0001 ohm), so the
        # same fixture becomes an RL high-pass without changing its topology.
        sim.cli('comp', 'set', 'C1', 'Capacitance=1 F')
        sim.cli('comp', 'set', coil, property_name + '=25 mH')
        sim.cli('comp', 'set', 'R1', 'Resistance=1600')
        step_errors = []
        for frequency in (10186, 51000):
            sim.cli('comp', 'set', 'GEN', 'Freq=%s Hz' % frequency)
            sim.cli('sim', 'run', '--for', '1 ms')
            vi, vl = sim.cli('sim', 'average', '--for', '%s ps' % round(50e12 / frequency),
                            'GEN.outnod', coil)['channels']
            gain = vl['voltage']['rms'] / vi['voltage']['rms']
            reactance = 2 * math.pi * frequency * 25e-3
            ideal = reactance / math.hypot(1600, reactance)
            step_errors.append(abs(gain - ideal))
            sim.log('RL high-pass %g Hz, h=%s: %.7f (ideal %.7f)' %
                    (frequency, step, gain, ideal))
            if step == '10 ns':
                sim.check('fine-step RL high-pass, %g Hz' % frequency, gain, ideal, tol=0.003)
        errors.append(step_errors)
    for i, frequency in enumerate((10186, 51000)):
        sim.check('RL high-pass error decreases with reactive step, %g Hz' % frequency,
                  errors[0][i] > errors[1][i] > errors[2][i], True, tol=0)


def dc_step(sim, transformer=False):
    if transformer:
        open_bandpass(sim, True, '1 us', '10 ns')
        sim.cli('comp', 'set', 'GEN', 'Semi_Ampli=0 V', 'Mid_Volt=10 V', 'Freq=1 Hz')
        sim.cli('comp', 'set', 'C1', 'Capacitance=1 F')
        sim.cli('comp', 'set', 'T1', 'BaseInd=100 uH')
        sim.cli('comp', 'set', 'R1', 'Resistance=100')
    else:
        sim.cli('circuit', 'open', os.path.join(CIRCUITS, 'rl_step.sim1'), '--discard')
        sim.cli('comp', 'set', 'L1', 'Inductance=100 uH')
        sim.cli('settings', 'set', 'stepSize=10 ns')
    currents = []
    for _ in range(12):
        sim.cli('sim', 'run', '--for', '1 us')
        r = sim.read('T1' if transformer else 'L1')
        currents.append(r['i_P01' if transformer else 'current'])
    sim.log('DC step currents [A]: %s' % currents)
    sim.check('DC RL step does not overshoot V/R', max(currents) <= 0.100001, True, tol=0)
    sim.check('DC RL step is monotonic',
              all(b >= a - 1e-6 for a, b in zip(currents, currents[1:])), True, tol=0)
    sim.check('DC RL step settles at V/R', currents[-1], 0.1, tol=0.001)


def idle_restart(sim):
    sim.cli('circuit', 'open', os.path.join(CIRCUITS, 'rl_step.sim1'), '--discard')
    sim.cli('sim', 'run', '--for', '1 s')
    sim.check('RL settled at 10 V', sim.read('L1')['current'], 0.1, tol=0.001)
    sim.cli('comp', 'set', 'V1', 'Voltage=20 V')
    sim.cli('sim', 'run', '--for', '100 ms')
    sim.check('idle inductor restarts after the next source step',
              sim.read('L1')['current'], 0.2, tol=0.001)


def free_decay(sim):
    sim.cli('circuit', 'open', os.path.join(CIRCUITS, 'lc_tank.sim1'), '--discard')
    sim.cli('comp', 'set', 'L1', 'Inductance=1 mH')
    sim.cli('comp', 'set', 'C1', 'Capacitance=10 uF')
    h, inductance, capacitance = 1e-6, 1e-3, 10e-6
    initial = 0.5 * capacitance * 10 ** 2
    previous = initial
    for _ in range(3):
        sim.cli('sim', 'run', '--for', '20 ms')
        volts = sim.read('C1')['voltage']
        amps = sim.read('L1')['current']
        energy = 0.5 * capacitance * volts ** 2 + 0.5 * inductance * amps ** 2
        elapsed = int(sim.cli('sim', 'status')['time_ps']) * 1e-12
        expected = initial * math.exp(-elapsed / h * math.log1p(h*h / (inductance*capacitance)))
        sim.check('free LC energy decays as BE predicts at %g ms' % (elapsed * 1e3),
                  energy, expected, tol=0.005)
        sim.check('free LC does not create energy at %g ms' % (elapsed * 1e3),
                  0 <= energy < previous, True, tol=0)
        previous = energy
