def padded(x, n):
    return np.concatenate([x, np.zeros(n - x.size)])
