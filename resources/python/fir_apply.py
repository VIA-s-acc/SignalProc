def fir_apply(x, h, centred):
    h = np.asarray(h, dtype=np.float64)
    start = (h.size - 1) // 2 if centred else 0
    rows = []
    for row in x:
        if not row.size:
            rows.append(row)
            continue
        full = np.convolve(row, h)
        piece = full[start : start + row.size]
        rows.append(np.concatenate([piece, np.zeros(row.size - piece.size)]))
    return np.stack(rows)
