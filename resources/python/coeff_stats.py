# needs: energy
# import: import math
def _coeff_stats_entropy(band):
    e = energy(band)
    h = 0.0
    if e > 0:
        for c in band.tolist():
            p = c * c / e
            if p > 0:
                h -= p * math.log2(p)
    return h


def coeff_stats(c, labels, weights, tol):
    report = {}
    for ch in range(c[0].shape[0]):
        bands = [b[ch] for b in c]
        e = [energy(b) * (weights[k] if k < len(weights) else 1.0) for k, b in enumerate(bands)]
        total = sum(e)
        peak = max([0.0] + [float(np.abs(b).max()) for b in bands if b.size])
        for k, (band, label) in enumerate(zip(bands, labels)):
            name = f"c{ch}.{label}"
            small = int(np.count_nonzero(np.abs(band) < tol * peak))
            report[name + ".energy"] = e[k] / total if total > 0 else 0.0
            report[name + ".entropy"] = _coeff_stats_entropy(band)
            report[name + ".sparsity"] = 0.0 if not band.size else small / band.size if peak > 0 else 1.0
    return report
