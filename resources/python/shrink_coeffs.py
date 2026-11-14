# import: import math
def _shrink_coeffs_sigma(d):
    m = np.abs(d[d != 0])
    return float(np.median(m)) / 0.6744897501960817 if m.size else 0.0


def _shrink_coeffs_threshold(rule, d, sigma, n):
    if rule == "visu":
        return sigma * math.sqrt(2 * math.log(n))
    if rule == "minimax":
        return 0.0 if n <= 32 or not sigma > 0 else sigma * (0.3936 + 0.1829 * math.log2(n))
    if rule == "bayes":
        if sigma == 0 or not d.size:
            return 0.0
        var = sigma * sigma
        return var / math.sqrt(max(math.fsum(d * d) / d.size - var, np.finfo(float).eps))
    size = d.size
    if size == 0 or not sigma > 0:
        return 0.0
    v = d / sigma
    sx2 = v * v
    norm2 = 0.0
    for s in sx2:
        norm2 += s
    universal = math.sqrt(2 * math.log(size))
    if (norm2 - size) / size < math.log2(size) ** 1.5 / math.sqrt(size):
        return sigma * universal
    sx2 = np.sort(sx2)
    i = np.arange(size)
    risk = (size - 2 * (i + 1) + np.cumsum(sx2) + (size - 1 - i) * sx2) / size
    return sigma * min(math.sqrt(sx2[int(np.argmin(risk))]), universal)


def shrink_coeffs(c, rule, mode, first, last, sigma, n, norm):
    out = [np.array(b, dtype=np.float64) for b in c]
    levels = len(c) - 1
    for ch in range(out[0].shape[0]):
        s = _shrink_coeffs_sigma(out[-1][ch]) if sigma is None else sigma
        if s == 0:
            continue
        for b in range(1, levels + 1):
            level = levels + 1 - b
            if level < first or level > last:
                continue
            ls = s * (2.0 ** (-0.5 * (level - 1)) if norm and level > 1 else 1.0)
            out[b][ch] = pywt.threshold(out[b][ch], _shrink_coeffs_threshold(rule, out[b][ch], ls, n), mode)
    return out
