def welch_psd(row, fs, segment):
    window = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(segment) / segment)
    scale = 1.0 / (fs * np.cumsum(window * window)[-1])
    starts = range(0, row.size - segment + 1, segment - segment // 2)
    psd = np.zeros(segment // 2 + 1)
    for s in starts:
        part = row[s : s + segment]
        spectrum = np.fft.rfft((part - sum(part.tolist()) / segment) * window)
        psd += (spectrum.real * spectrum.real + spectrum.imag * spectrum.imag) * scale
    psd /= len(starts)
    psd[1 : psd.size - (segment % 2 == 0)] *= 2
    return psd
