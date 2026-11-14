def trim_silence(x, threshold_db):
    threshold = 10.0 ** (threshold_db / 20)
    loud = np.any(np.abs(x) >= threshold, axis=0)
    idx = np.flatnonzero(loud)
    if not idx.size:
        return x[:, :0]
    return x[:, idx[0] : idx[-1] + 1]
