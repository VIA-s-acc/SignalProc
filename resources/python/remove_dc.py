# import: import math
def remove_dc(x):
    out = np.array(x, dtype=np.float64)
    for c in range(out.shape[0]):
        if out[c].size:
            out[c] = out[c] - math.fsum(out[c]) / out[c].size
    return out
