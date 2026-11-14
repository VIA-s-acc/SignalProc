# needs: correlate_full
# import: import math
def _pitch_frame(seg, w, tau_min, top, fs, threshold):
    cross = correlate_full(seg[: w + top], seg[:w])[w - 1 : w + top]
    e0 = np.cumsum(seg[:w] * seg[:w])[-1]
    cmnd = [1.0] * (top + 1)
    shifted, acc = e0, 0.0
    for tau in range(1, top + 1):
        shifted += seg[tau - 1 + w] * seg[tau - 1 + w] - seg[tau - 1] * seg[tau - 1]
        d = max(0.0, e0 + shifted - 2 * cross[tau])
        acc += d
        cmnd[tau] = d * tau / acc if acc > 0 else 1.0
    tau = tau_min
    while tau <= top and not cmnd[tau] < threshold:
        tau += 1
    if tau > top:
        return 0.0
    while tau + 1 <= top and cmnd[tau + 1] < cmnd[tau]:
        tau += 1
    delta = 0.0
    if tau >= 2 and tau + 1 <= top:
        a, b, c = cmnd[tau - 1], cmnd[tau], cmnd[tau + 1]
        if a - 2 * b + c > 0:
            delta = 0.5 * (a - c) / (a - 2 * b + c)
    return fs / (tau + delta)


def _pitch_median(f0):
    voiced = sorted(f for f in f0 if f > 0)
    h = len(voiced) // 2
    return 0.0 if not voiced else voiced[h] if len(voiced) % 2 else 0.5 * (voiced[h - 1] + voiced[h])


def pitch(x, fs, frame, hop, fmin, fmax, threshold):
    w, tau_min, top = frame // 2, max(1, math.floor(fs / fmax)), math.ceil(fs / fmin)
    count = (x.shape[1] - frame) // hop + 1 if x.shape[1] >= frame else 0
    report, tracks = {}, np.zeros((x.shape[0], count))
    for ch, row in enumerate(x):
        suffix = f"_ch{ch}" if x.shape[0] > 1 else ""
        f0 = [_pitch_frame(row[k * hop : k * hop + frame], w, tau_min, top, fs, threshold) for k in range(count)]
        report["f0_median_hz" + suffix] = _pitch_median(f0)
        report["voiced_fraction" + suffix] = sum(1 for f in f0 if f > 0) / count if count else 0.0
        report["frames" + suffix] = float(count)
        tracks[ch] = f0
    return report, tracks
