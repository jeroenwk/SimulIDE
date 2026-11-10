"""BH-053 re-evaluated: retain backward Euler, reduce the reactive step for accuracy.

The 0.8663 resonance gain at 1us is the discrete circuit's expected numerical
damping, not a phantom physical resistor. Check it against independent BE
impedances, then check convergence towards continuous physics at 100ns / 10ns.
Protect coarse DC monotonicity, idle restart, and the passive LC energy decay.
See reactive_checks.py for equations and the rejected PR's failure modes.
"""

from harness import run_test
from reactive_checks import dc_step, free_decay, frequency_response, idle_restart, rl_highpass


def test(sim):
    frequency_response(sim)
    rl_highpass(sim)
    dc_step(sim)
    idle_restart(sim)
    free_decay(sim)


if __name__ == '__main__':
    run_test(test)
