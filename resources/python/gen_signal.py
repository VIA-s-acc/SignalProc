# needs: samples_count, portable_random, noise_from_draws, sweep, donoho_johnstone
def gen_signal(kind, freq, f1, amplitude, fs, duration, seed, channels):
    n = samples_count(duration, fs)
    if kind in ("white", "pink"):
        return np.stack([noise_from_draws(portable_random(seed + c, n), kind, amplitude) for c in range(channels)])
    k = np.arange(n)
    if kind == "sine":
        x = amplitude * np.sin(2 * np.pi * freq * k / fs)
    elif kind == "sweep":
        x = sweep(freq, f1, True, fs, n)
    elif kind == "impulse":
        x = (k == 0) * 1.0
    elif kind == "step":
        x = np.ones(n)
    elif kind == "harmonics":
        x = sum(amplitude / 2 ** h * np.sin(2 * np.pi * ((h + 1) * freq) * k / fs) for h in range(4))
    else:
        x = donoho_johnstone(kind, n) * amplitude
    return np.tile(x, (channels, 1))
