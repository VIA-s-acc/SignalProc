# import: import math
def histogram(x, bins, limits):
    report = {}
    for ch, row in enumerate(x):
        suffix = f"_ch{ch}" if x.shape[0] > 1 else ""
        lo, hi = limits if limits is not None else (float(row.min()), float(row.max())) if row.size else (0.0, 1.0)
        if lo == hi:
            lo, hi = lo - 0.5, hi + 0.5
        scale = 1.0 if math.isfinite(hi - lo) else 0.5
        span = hi * scale - lo * scale
        edges = [(i * (span / bins) + lo * scale) / scale for i in range(bins)] + [hi]
        counts = [0] * bins
        for v in row.tolist():
            position = (v * scale - lo * scale) / span * bins
            if not lo <= v <= hi or not math.isfinite(position):
                continue
            k = int(min(max(position, 0.0), bins - 1))
            if k > 0 and v < edges[k]:
                k -= 1
            if k + 1 < bins and v >= edges[k + 1]:
                k += 1
            counts[k] += 1
        fullest = counts.index(max(counts))
        report["lo" + suffix], report["hi" + suffix] = lo, hi
        report["counts" + suffix] = " ".join(str(c) for c in counts)
        report["in_range" + suffix] = float(sum(counts))
        report["outside" + suffix] = float(row.size - sum(counts))
        report["mode" + suffix] = (edges[fullest] + edges[fullest + 1]) / 2
    return report
