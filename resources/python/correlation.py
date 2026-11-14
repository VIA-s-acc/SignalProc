# needs: energy
# import: import math
# import: from scipy import stats as sp_stats
def _correlation_pearson(a, b):
    ma, mb = np.cumsum(a)[-1] / a.size, np.cumsum(b)[-1] / b.size
    sab, saa, sbb = (np.cumsum((u - mu) * (v - mv))[-1] for u, mu, v, mv in ((a, ma, b, mb), (a, ma, a, ma), (b, mb, b, mb)))
    return min(max(sab / math.sqrt(saa * sbb), -1.0), 1.0) if saa > 0 and sbb > 0 else 0.0


def correlation(a, b):
    report = {}
    for ch in range(a.shape[0]):
        suffix = f"_ch{ch}" if a.shape[0] > 1 else ""
        y = b[min(ch, b.shape[0] - 1)]
        n = min(a.shape[1], y.size)
        x, z = a[ch, :n], y[:n]
        energies = energy(x) * energy(z)
        report["pearson" + suffix] = float(_correlation_pearson(x, z))
        report["spearman" + suffix] = float(_correlation_pearson(sp_stats.rankdata(x), sp_stats.rankdata(z)))
        report["cosine" + suffix] = float(min(max(np.cumsum(x * z)[-1] / math.sqrt(energies), -1.0), 1.0)) if energies > 0 else 0.0
        report["n" + suffix] = float(n)
    return report
