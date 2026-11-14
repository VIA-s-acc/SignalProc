# import: import math
def _lpc_levinson(r, order):
    a, k = np.zeros(order + 1), np.zeros(order)
    if not r[0] > 0:
        return a[1:], k, 0.0
    a[0], error = 1.0, r[0]
    for m in range(1, order + 1):
        acc = r[m]
        for j in range(1, m):
            acc += a[j] * r[m - j]
        k[m - 1] = -acc / error
        previous = a.copy()
        for j in range(1, m):
            a[j] = previous[j] + k[m - 1] * previous[m - j]
        a[m] = k[m - 1]
        error *= 1 - k[m - 1] * k[m - 1]
    return a[1:], k, 10 * math.log10(r[0] / error)


def lpc(x, order):
    residual = np.empty_like(x)
    report = {"order": float(order)}
    for ch, row in enumerate(x):
        suffix = f"_ch{ch}" if x.shape[0] > 1 else ""
        r = [np.cumsum(row[lag:] * row[: row.size - lag])[-1] if lag < row.size else 0.0 for lag in range(order + 1)]
        a, k, gain_db = _lpc_levinson(r, order)
        prediction = np.zeros(row.size)
        for j in range(order, 0, -1):
            prediction[j:] += a[j - 1] * row[: row.size - j]
        residual[ch] = row + prediction
        report["gain_db" + suffix] = float(gain_db)
        report.update({f"a{j + 1}{suffix}": float(a[j]) for j in range(order)})
        report.update({f"k{j + 1}{suffix}": float(k[j]) for j in range(order)})
    return residual, report
