def shifted(row, d):
    n = row.size
    out = np.zeros(n)
    lo, hi = max(0, d), min(n, n + d)
    if lo < hi:
        out[lo:hi] = row[lo - d : hi - d]
    return out
