"""BH-055 re-evaluated: transformer windings must keep the stable BE update.

The one-primary/no-secondary fixture checks numerical damping at 1us and
convergence at smaller reactive steps. A DC step in a strongly overdamped
loop must remain monotonic, including when the reactive step equals L/R.
The PR's cached-voltage update introduces 12.5% overshoot in that case.
"""

from harness import run_test
from reactive_checks import dc_step, frequency_response, rl_highpass


def test(sim):
    frequency_response(sim, transformer=True)
    rl_highpass(sim, transformer=True)
    dc_step(sim, transformer=True)


if __name__ == '__main__':
    run_test(test)
