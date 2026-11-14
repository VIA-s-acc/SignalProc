def correlate_full(x, y):
    n = x.size + y.size - 1
    if x.size * y.size <= 1 << 16:
        out = np.zeros(n)
        for i in range(x.size):
            out[i : i + y.size] += x[i] * y[::-1]
        return out
    return np.fft.ifft(np.fft.fft(x, n) * np.fft.fft(y[::-1], n)).real
