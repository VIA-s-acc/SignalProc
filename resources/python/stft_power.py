def stft_power(row, n, hop):
    window = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n) / n)
    scale = 1.0 / (np.cumsum(window)[-1] ** 2)
    frames = []
    for start in range(0, row.size - n + 1, hop):
        spectrum = np.fft.rfft(row[start : start + n] * window)
        frames.append((spectrum.real * spectrum.real + spectrum.imag * spectrum.imag) * scale)
    return frames
