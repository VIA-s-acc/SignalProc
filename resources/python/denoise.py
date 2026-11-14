# needs: shrink_coeffs
def _denoise_once(v, wavelet, levels, rule, mode):
    c = pywt.wavedec(v, wavelet, mode="symmetric", level=levels)
    sigma = _shrink_coeffs_sigma(c[-1])
    if sigma == 0:
        return np.array(v, dtype=np.float64)
    t = [_shrink_coeffs_threshold(rule, b, sigma, v.size) for b in c[1:]]
    c = [c[0]] + [pywt.threshold(b, tb, mode) for b, tb in zip(c[1:], t)]
    return pywt.waverec(c, wavelet, mode="symmetric")[: v.size]


def denoise(x, wavelet, levels, rule, mode, shifts):
    rows = []
    for row in x:
        if shifts == 1:
            rows.append(_denoise_once(row, wavelet, levels, rule, mode))
            continue
        total = np.zeros(row.size)
        for s in range(shifts):
            total = total + np.roll(_denoise_once(np.roll(row, s), wavelet, levels, rule, mode), -s)
        rows.append(total / shifts)
    return np.stack(rows)
