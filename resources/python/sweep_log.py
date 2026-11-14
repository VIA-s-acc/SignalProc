def sweep_log(f0, f1, fs, n):
    if n < 2:
        return np.ones(n)
    t1 = (n - 1) / fs
    t = np.arange(n) / fs
    if f0 == f1:
        return np.cos(2 * np.pi * f0 * t)
    beta = t1 / np.log(f1 / f0)
    return np.cos(2 * np.pi * beta * f0 * ((f1 / f0) ** (t / t1) - 1.0))
