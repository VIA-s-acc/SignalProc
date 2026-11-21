def samples_signal(values):
    rows = [part.replace(",", " ").replace(";", " ").split() for part in str(values).split("|")]
    return np.array([[float(v) for v in row] for row in rows], dtype=np.float64)
