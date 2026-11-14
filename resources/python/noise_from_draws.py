def noise_from_draws(x, color, rms):
    x = np.array(x, dtype=np.float64)
    n = x.size
    if n and color != "white":
        spectrum = np.fft.rfft(x)
        spectrum[0] = 0
        k = np.arange(1, spectrum.size, dtype=np.float64)
        spectrum[1:] *= k ** (-0.5 if color == "pink" else -1.0)
        x = np.fft.irfft(spectrum, n)
    if n:
        current = np.sqrt(np.sum(x * x) / n)
        if current != 0:
            x = x * (rms / current)
    return x
