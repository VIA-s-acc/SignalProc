_donoho_johnstone_positions = [0.1, 0.13, 0.15, 0.23, 0.25, 0.4, 0.44, 0.65, 0.76, 0.78, 0.81]
_donoho_johnstone_blocks = [4.0, -5.0, 3.0, -4.0, 5.0, -4.2, 2.1, 4.3, -3.1, 2.1, -4.2]
_donoho_johnstone_bumps = [4.0, 5.0, 3.0, 4.0, 5.0, 4.2, 2.1, 4.3, 3.1, 5.1, 4.2]
_donoho_johnstone_widths = [0.005, 0.005, 0.006, 0.01, 0.01, 0.03, 0.01, 0.01, 0.005, 0.008, 0.005]


def donoho_johnstone(kind, n):
    dt = 1.0 / n
    t = dt + np.arange(n, dtype=np.float64) * dt
    f = np.zeros(n)
    if kind == "blocks":
        for pos, height in zip(_donoho_johnstone_positions, _donoho_johnstone_blocks):
            f = f + height * (1 + np.sign(t - pos)) / 2
        return f
    if kind == "bumps":
        for pos, height, width in zip(_donoho_johnstone_positions, _donoho_johnstone_bumps, _donoho_johnstone_widths):
            f = f + height / (1 + np.abs((t - pos) / width)) ** 4
        return f
    if kind == "heavisine":
        return 4 * np.sin(4 * np.pi * t) - np.sign(t - 0.3) - np.sign(0.72 - t)
    return np.sqrt(t * (1 - t)) * np.sin(2 * np.pi * 1.05 / (t + 0.05))
