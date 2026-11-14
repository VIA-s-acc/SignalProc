def lpc_synth(e, report):
    order = int(report["order"])
    y = np.empty_like(e)
    for ch, row in enumerate(e):
        suffix = f"_ch{ch}" if e.shape[0] > 1 else ""
        a = [report[f"a{j + 1}{suffix}"] for j in range(order)]
        out = []
        for n, v in enumerate(row.tolist()):
            acc = 0.0
            for j in range(min(order, n), 0, -1):
                acc += a[j - 1] * out[n - j]
            out.append(v - acc)
        y[ch] = out
    return y
