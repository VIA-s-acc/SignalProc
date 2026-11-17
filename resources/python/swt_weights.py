def swt_weights(c, meta):
    if meta["transform"] != "swt":
        return []
    levels = len(c) - 1
    if meta["norm"]:
        return [1.0] * (levels + 1)
    return [2.0 ** -levels] + [2.0 ** -(levels + 1 - k) for k in range(1, levels + 1)]
