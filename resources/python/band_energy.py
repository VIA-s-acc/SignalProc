# import: import math
def band_energy(c, labels, weights):
    out = {}
    for ch in range(c[0].shape[0]):
        e = [math.fsum(b[ch] * b[ch]) * (weights[k] if k < len(weights) else 1.0) for k, b in enumerate(c)]
        total = math.fsum(e)
        for k, label in enumerate(labels):
            out[f"c{ch}.{label}"] = e[k] / total if total > 0 else 0.0
    return out
