def add_signals(a, b, sign):
    channels = max(a.shape[0], b.shape[0])
    out = np.zeros((channels, max(a.shape[1], b.shape[1])))
    for c in range(channels):
        if a.shape[0]:
            row = a[min(c, a.shape[0] - 1)]
            out[c, : row.size] += row
        if b.shape[0]:
            row = b[min(c, b.shape[0] - 1)]
            out[c, : row.size] += sign * row
    return out
