def delay_lines(x, kind, delay, gain, mix=0.0):
    rows = []
    for row in x:
        if kind == "echo":
            e = np.zeros(row.size)
            for n in range(delay, row.size):
                e[n] = row[n - delay] + gain * e[n - delay]
            rows.append((1 - mix) * row + mix * e)
            continue
        y = np.array(row, dtype=np.float64)
        for n in range(delay, row.size):
            y[n] = row[n] + gain * (y[n - delay] if kind == "feedback" else row[n - delay])
        rows.append(y)
    return np.stack(rows)
