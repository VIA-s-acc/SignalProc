def merge_signals(left, right):
    rows = list(left) + list(right)
    frames = max(row.size for row in rows)
    return np.stack([np.concatenate([row, np.zeros(frames - row.size)]) for row in rows])
