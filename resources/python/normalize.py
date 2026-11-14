# import: import math
def normalize(x, kind, target_dbfs):
    out = np.array(x, dtype=np.float64)
    for c in range(out.shape[0]):
        row = out[c]
        if not row.size:
            continue
        level = float(np.max(np.abs(row))) if kind == "peak" else math.sqrt(math.fsum(row * row) / row.size)
        if level != 0:
            out[c] = row * (10 ** (target_dbfs / 20) / level)
    return out
