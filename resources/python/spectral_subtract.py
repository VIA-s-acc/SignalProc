# needs: stft
def spectral_subtract(x, noise, lead, n, alpha, beta):
    hop = max(1, n // 4)
    out = np.empty_like(x)
    for ch, row in enumerate(x):
        segment = row[:lead] if noise is None else noise[min(ch, noise.shape[0] - 1)]
        level = np.zeros(n // 2 + 1)
        frames = stft(segment, n, hop)
        for frame in frames:
            level += frame.real * frame.real + frame.imag * frame.imag
        level /= len(frames)
        z = stft(row, n, hop)
        power = np.maximum(z.real * z.real + z.imag * z.imag - alpha * level, beta * level)
        magnitude = np.hypot(z.real, z.imag)
        with np.errstate(all="ignore"):
            z = np.where(magnitude > 0, z * (np.sqrt(power) / magnitude), np.sqrt(power) + 0j)
        out[ch] = istft(z, n, hop)[: row.size]
    return out
