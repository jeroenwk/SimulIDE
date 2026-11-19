"""Reference for the fuse tests: the heat model of specs/fuse.md § 4, integrated numerically.

Independent of the C++ code (src/simulator/elements/passive/e-fuseheat.cpp), which solves the
same equations in closed form: here a plain RK4 with a small fixed step. Shared with the C++
part are only the input numbers (presets and datasheet table values), copied below.

    C1 dθ/dt = u - G1 (θ - φ)          u = I² (A²), melts at θ = 1
    C2 dφ/dt = G1 (θ - φ) - G2 φ
"""

# Speed class: C1/I²t, I_mf/In, G2/G1, τ2 (s)   [INFERRED specs/fuse/fuse_fit.py]
PRESET = {"F": (0.9284, 1.600, 0.6874, 1735.5), "T": (1.0362, 2.000, 0.9706, 22.0)}
# Melting I²t of the 1 A ratings and their voltage drop at In  [DOC Schurter FSF / FST 5x20]
I2T_1A = {"F": 1.13, "T": 3.3}
VDROP_1A = {"F": 0.110, "T": 0.060}


class Heat:
    def __init__(self, speed, i_n=1.0, i2t=None):
        c1r, mf, share, tau2 = PRESET[speed]
        i2t = i2t if i2t else I2T_1A[speed] * i_n * i_n   # only 1 A ratings are used by the tests
        gt = (mf * i_n) ** 2
        self.g1 = gt * (1 + 1 / share)
        self.g2 = share * self.g1
        self.c1 = c1r * i2t
        self.c2 = tau2 * self.g2
        self.th = self.ph = 0.0

    def tau1(self):
        return self.c1 / self.g1

    def deriv(self, th, ph, u):
        return ((u - self.g1 * (th - ph)) / self.c1, (self.g1 * (th - ph) - self.g2 * ph) / self.c2)

    def step(self, dt, u):
        th, ph = self.th, self.ph
        k1 = self.deriv(th, ph, u)
        k2 = self.deriv(th + dt / 2 * k1[0], ph + dt / 2 * k1[1], u)
        k3 = self.deriv(th + dt / 2 * k2[0], ph + dt / 2 * k2[1], u)
        k4 = self.deriv(th + dt * k3[0], ph + dt * k3[1], u)
        self.th = th + dt / 6 * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0])
        self.ph = ph + dt / 6 * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1])

    def run(self, u_of_t, t_end, dt):
        """Integrate to t_end with u(t) (A²); return the melting time (interpolated) or None."""
        t = 0.0
        while t < t_end - 1e-15:
            h = min(dt, t_end - t)
            th0 = self.th
            self.step(h, u_of_t(t + h / 2))
            if self.th >= 1:
                return t + h * (1 - th0) / (self.th - th0)
            t += h
        return None

    def melt_time(self, u, t_max, dt=None):
        """Melting time at constant u from cold; None if it does not melt within t_max."""
        dt = dt or min(self.tau1() / 400, t_max / 4000)
        return self.run(lambda t: u, t_max, dt)
