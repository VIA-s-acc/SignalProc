def samples_count(seconds, fs):
    x = seconds * fs
    q = np.floor(x)
    return int(q) + (1 if x - q >= 0.5 else 0)
