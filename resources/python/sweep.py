# import: import math
def sweep(f0, f1, log, fs, n):
    if n < 2:
        return np.ones(n)
    t1 = (n - 1) / fs
    t = np.arange(n) / fs
    if not log:
        beta = (f1 - f0) / t1
        return np.cos(2 * np.pi * (f0 * t + 0.5 * beta * t * t))
    if f0 == f1:
        return np.cos(2 * np.pi * f0 * t)
    beta = t1 / math.log(f1 / f0)
    growth = np.array([math.pow(f1 / f0, u) for u in (t / t1).tolist()])
    return np.cos(2 * np.pi * beta * f0 * (growth - 1.0))
