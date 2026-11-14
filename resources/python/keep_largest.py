# import: import math
def keep_largest(c, percent, amount, first):
    out = [np.array(b, dtype=np.float64) for b in c]
    for ch in range(out[0].shape[0]):
        pool = np.concatenate([b[ch] for b in out[first:]]) if len(out) > first else np.zeros(0)
        total = pool.size
        keep = min(total, int(math.floor((amount / 100 * total if percent else amount) + 0.5)))
        order = np.argsort(-np.abs(pool), kind="stable")
        pool[order[keep:]] = 0.0
        at = 0
        for b in out[first:]:
            b[ch] = pool[at : at + b.shape[1]]
            at += b.shape[1]
    return out
