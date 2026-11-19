"""Fit the fuse heat model to the Schurter FSF / FST 5x20 pre-arcing limits (In = 1 A).

Two-stage model, input u = I^2 (A^2), melt at theta = 1:
    C1 dθ/dt = u - G1 (θ - φ)          element
    C2 dφ/dt = G1 (θ - φ) - G2 φ        body / end caps -> ambient
One stage = C2 -> 0 (φ follows θ) : C1 dθ/dt = u - G θ.
Fit in log-time to the middle (geometric) of each IEC min/max window, plus the datasheet's
I2t at 10 In; 1.5 In must never blow (I_mf > 1.5 In) and 2.1 In must blow within its limit.
"""
import numpy as np
from scipy.optimize import minimize
from scipy.linalg import expm

H = 3600.0
# multiple: (t_min, t_max)   [DOC] Schurter FSF / FST datasheets, 0.125-6.3 A rows
FSF = dict(i2t=1.13, gates={2.1: (None, 1800), 2.75: (0.05, 2), 4: (0.01, 0.3), 10: (None, 0.02)})
FST = dict(i2t=3.3, gates={2.1: (None, 120), 2.75: (0.6, 10), 4: (0.15, 3), 10: (0.02, 0.3)})


def t_blow(p, m, two=True, tmax=1e5):
    """Time to melt from cold at constant current m (A, In = 1)."""
    u = m * m
    if not two:
        C1, G = p[:2]
        th = u / G
        return np.inf if th <= 1 else -C1 / G * np.log(1 - 1 / th)
    C1, G1, C2, G2 = p
    A = np.array([[-G1 / C1, G1 / C1], [G1 / C2, -(G1 + G2) / C2]])
    b = np.array([u / C1, 0.0])
    xss = -np.linalg.solve(A, b)
    if xss[0] <= 1:
        return np.inf
    w, V = np.linalg.eig(A)
    c = np.linalg.solve(V, -xss)            # x(t) = xss + V diag(e^wt) c
    th = lambda t: xss[0] + (V[0] * c * np.exp(w * t)).sum().real
    lo, hi = 0.0, 1e-6
    while th(hi) < 1:
        lo, hi = hi, hi * 2
        if hi > tmax:
            return np.inf
    for _ in range(80):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if th(mid) < 1 else (lo, mid)
    return hi


def target(win):
    lo, hi = win
    return np.sqrt(lo * hi) if lo else hi / 3


def cost(lp, spec, two):
    p = np.exp(lp)
    imf = np.sqrt(p[1]) if not two else 1 / np.sqrt(1 / p[1] + 1 / p[3])
    if not 1.6 <= imf <= 2.0:
        return 1e6 + abs(imf - 1.8)
    e = 0.0
    for m, win in spec["gates"].items():
        t = t_blow(p, m, two)
        lo, hi = win
        if not np.isfinite(t):
            return 1e6
        e += np.log(t / target(win)) ** 2
        if t > hi: e += 50 * np.log(t / hi) ** 2
        if lo and t < lo: e += 50 * np.log(lo / t) ** 2
    t15 = t_blow(p, 1.5, two)
    if np.isfinite(t15) and t15 < H: e += 50 * np.log(H / t15) ** 2
    t10 = t_blow(p, 10, two)
    e += 20 * np.log(100 * t10 / spec["i2t"]) ** 2          # I2t at 10 In
    return e


def fit(spec, two):
    best = None
    i2t = spec["i2t"]
    for g in (2.8, 3.2, 3.6):
        x0 = [np.log(i2t), np.log(g)] + ([np.log(g * 1.5), np.log(i2t * 20), np.log(g * 1.5)][:0] if not two else [])
        if two:
            x0 = [np.log(i2t), np.log(4 * g), np.log(30 * i2t), np.log(1.3 * g)]
        r = minimize(cost, x0, args=(spec, two), method="Nelder-Mead",
                     options=dict(maxiter=20000, xatol=1e-6, fatol=1e-9))
        if best is None or r.fun < best.fun:
            best = r
    return np.exp(best.x), best.fun


for name, spec in (("FSF (fast)", FSF), ("FST (time-lag)", FST)):
    for two in (False, True):
        p, f = fit(spec, two)
        print("%s, %s stage(s): params %s  cost %.3f" % (name, 2 if two else 1, np.round(p, 4), f))
        if two:
            C1, G1, C2, G2 = p
            print("   I_mf = %.2f In, tau1 = C1/G1 = %.1f ms, tau2 ~ C2/G2 = %.2f s" %
                  (1 / np.sqrt(1 / G1 + 1 / G2), 1e3 * C1 / G1, C2 / G2))
        else:
            print("   I_mf = %.2f In, tau = %.1f ms" % (np.sqrt(p[1]), 1e3 * p[0] / p[1]))
        for m in (1.5, 2.1, 2.75, 4, 10):
            t = t_blow(p, m, two)
            win = spec["gates"].get(m, (H, None) if m == 1.5 else None)
            ok = ""
            if m == 1.5:
                ok = "ok" if t > H else "FAIL"
            else:
                lo, hi = win
                ok = "ok" if (t <= hi and (lo is None or t >= lo)) else "FAIL"
            print("   %5.2f In: %10.4g s   limit %-16s %s" % (m, t, str(win), ok))
        print("   I2t at 10 In: %.3g A2s (datasheet %.3g)" % (100 * t_blow(p, 10, two), spec["i2t"]))
