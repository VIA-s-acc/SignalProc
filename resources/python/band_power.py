# import: from scipy import signal
def band_power(x, fs, third, segment):
    if third:
        names, step, first = ["25", "31.5", "40", "50", "63", "80", "100", "125", "160", "200", "250", "315", "400", "500", "630", "800", "1k", "1.25k", "1.6k",
                              "2k", "2.5k", "3.15k", "4k", "5k", "6.3k", "8k", "10k", "12.5k", "16k", "20k"], 1.0, -16.0
    else:
        names, step, first = ["31.5", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"], 3.0, -5.0
    edges = [1000 * 10.0 ** ((step * (first + i) - step / 2) / 10) for i in range(len(names) + 1)]
    bands = sum(1 for e in edges[: len(names)] if e < fs / 2)
    out = {}
    for ch, row in enumerate(x):
        suffix = f"_ch{ch}" if x.shape[0] > 1 else ""
        length = min(row.size, segment)
        psd = signal.welch(row, fs, window="hann", nperseg=length, noverlap=length // 2)[1] if length >= 2 else np.zeros(0)
        power = np.zeros(len(names) + 2)
        np.add.at(power, np.searchsorted(edges, np.arange(psd.size) * (fs / length), side="right"), psd)
        total = np.cumsum(psd)[-1] if psd.size else 0.0
        share = lambda v: float(v / total) if total > 0 else 0.0
        out["below_22hz" + suffix] = share(power[0])
        for i in range(bands):
            out[names[i] + "hz" + suffix] = share(power[i + 1])
        out["above_22khz" + suffix] = share(power[-1])
    return out
