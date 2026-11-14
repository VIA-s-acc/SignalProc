def deconvolve(y, h, snr_db):
    floor = 10.0 ** (-snr_db / 10)
    out = np.empty_like(y)
    for ch, row in enumerate(y):
        Y = np.fft.fft(row)
        H = np.fft.fft(h[min(ch, h.shape[0] - 1)], row.size)
        X = Y * np.conj(H)
        power = H.real * H.real + H.imag * H.imag + floor
        out[ch] = np.fft.ifft(X.real / power + 1j * (X.imag / power)).real
    return out
