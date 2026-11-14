def pairwise(a, b, combine):
    rows = []
    for c in range(max(a.shape[0], b.shape[0])):
        x = a[min(c, a.shape[0] - 1)] if a.shape[0] else np.zeros(0)
        y = b[min(c, b.shape[0] - 1)] if b.shape[0] else np.zeros(0)
        rows.append(combine(x, y))
    width = max(r.size for r in rows) if rows else 0
    return np.stack([np.concatenate([r, np.zeros(width - r.size)]) for r in rows]) if rows else np.zeros((0, 0))
