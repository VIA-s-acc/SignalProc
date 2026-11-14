# needs: sst
def sst_ridge(x, fs, lo, hi, voices, mu):
    report, freq, amp = {}, np.zeros_like(x), np.zeros_like(x)
    for ch, row in enumerate(x):
        suffix = f"_ch{ch}" if x.shape[0] > 1 else ""
        freq[ch], amp[ch] = sst(row, fs, lo, hi, voices, mu, True)
        found = sorted(f for f in freq[ch].tolist() if f > 0)
        h = len(found) // 2
        report["freq_median_hz" + suffix] = 0.0 if not found else found[h] if len(found) % 2 else 0.5 * (found[h - 1] + found[h])
        report["freq_min_hz" + suffix] = found[0] if found else 0.0
        report["freq_max_hz" + suffix] = found[-1] if found else 0.0
        report["found_fraction" + suffix] = len(found) / row.size
        report["amp_peak" + suffix] = float(amp[ch].max())
    return report, freq, amp
