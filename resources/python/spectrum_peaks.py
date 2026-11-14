# needs: welch_psd
# import: from scipy import signal
def spectrum_peaks(x, fs, count, segment, prominence):
    out = {}
    for ch, row in enumerate(x):
        suffix = f"_ch{ch}" if x.shape[0] > 1 else ""
        length = min(row.size, segment)
        db = 10 * np.log10(np.maximum(welch_psd(row, fs, length), 1e-20)) if length >= 2 else np.zeros(0)
        peaks = sorted(signal.find_peaks(db, prominence=prominence)[0], key=lambda k: -db[k])[:count]
        out["peaks" + suffix] = float(len(peaks))
        for i, k in enumerate(peaks, start=1):
            out[f"peak{i}_hz{suffix}"] = float(k) * (fs / length)
            out[f"peak{i}_db{suffix}"] = float(db[k])
    return out
