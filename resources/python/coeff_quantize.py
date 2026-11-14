def coeff_quantize(c, by_bits, bits, step):
    levels = 2.0 ** (bits - 1) - 1
    out = [np.array(b, dtype=np.float64) for b in c]
    for ch in range(out[0].shape[0]):
        for b in out[1:]:
            delta = float(np.max(np.abs(b[ch]))) / levels if by_bits and b[ch].size else step
            if not delta > 0:
                continue
            b[ch] = np.copysign(np.floor(np.abs(b[ch]) / delta + 0.5), b[ch]) * delta
    return out
