# import: import math
def phaser(x, fs, stages, rate, lo, hi, feedback, mix):
    rows = []
    for row in x:
        xs, ys, y = [0.0] * stages, [0.0] * stages, np.zeros(row.size)
        last = 0.0
        for k in range(row.size):
            fc = lo * (hi / lo) ** ((1 - math.cos(2 * math.pi * rate * k / fs)) / 2)
            t = math.tan(math.pi * fc / fs)
            a = (t - 1) / (t + 1)
            u = row[k] + feedback * last
            for s in range(stages):
                v = a * (u - ys[s]) + xs[s]
                xs[s], ys[s], u = u, v, v
            last = u
            y[k] = (1 - mix) * row[k] + mix * u
        rows.append(y)
    return np.stack(rows)
