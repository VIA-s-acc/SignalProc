def _stft_window(n):
    window = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n) / n)
    return window, np.cumsum(window)[-1]


def stft(row, n, hop):
    window, total = _stft_window(n)
    padded = np.concatenate([np.zeros(n // 2), row, np.zeros(n // 2)])
    padded = np.pad(padded, (0, max(0, n - padded.size)))
    padded = np.pad(padded, (0, (hop - (padded.size - n) % hop) % hop))
    return np.array([np.fft.rfft(padded[s : s + n] * window) * (1.0 / total) for s in range(0, padded.size - n + 1, hop)])


def istft(frames, n, hop):
    window, total = _stft_window(n)
    length = (len(frames) - 1) * hop + n
    out, norm = np.zeros(length), np.zeros(length)
    for m, frame in enumerate(frames):
        out[m * hop : m * hop + n] += np.fft.irfft(frame, n, norm="forward") * (total / n) * window
        norm[m * hop : m * hop + n] += window * window
    np.divide(out, norm, out=out, where=norm > 1e-10)
    return out[n // 2 : length - n // 2] if length > 2 * (n // 2) else np.zeros(0)
