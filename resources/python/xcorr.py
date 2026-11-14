def xcorr(a, b, mode, normalize):
    rows = []
    for c in range(a.shape[0]):
        x, y = a[c], b[min(c, b.shape[0] - 1)]
        full = np.correlate(x, y, "full")
        start, length = 0, full.size
        if mode == "same":
            length = x.size
            start = (full.size - length) // 2
        elif mode == "valid":
            length = max(x.size, y.size) - min(x.size, y.size) + 1
            start = min(x.size, y.size) - 1
        r = full[start : start + length]
        if normalize:
            ea = eb = 0.0
            for v in x:
                ea += v * v
            for v in y:
                eb += v * v
            if ea > 0 and eb > 0:
                r = r / np.sqrt(ea * eb)
        rows.append(r)
    return np.stack(rows)
