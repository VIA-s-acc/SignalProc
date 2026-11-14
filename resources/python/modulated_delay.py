# import: import math
def modulated_delay(x, fs, rate, depth_ms, base_ms, feedback, mix):
    rows = []
    for row in x:
        line, y = np.zeros(row.size), np.zeros(row.size)
        for k in range(row.size):
            d = max(1.0, (base_ms + depth_ms * (1 + math.sin(2 * math.pi * rate * k / fs)) / 2) * fs / 1000)
            pos = k - d
            i0 = math.floor(pos)
            frac = pos - i0
            at = lambda i: line[int(i)] if i >= 0 else 0.0
            wet = (1 - frac) * at(i0) + (frac * at(i0 + 1) if frac > 0 else 0.0)
            line[k] = row[k] + feedback * wet
            y[k] = (1 - mix) * row[k] + mix * wet
        rows.append(y)
    return np.stack(rows)
