def median_filter(x, kernel):
    half = kernel // 2
    rows = []
    for row in x:
        padded = np.concatenate([np.zeros(half), row, np.zeros(half)])
        windows = np.lib.stride_tricks.sliding_window_view(padded, min(kernel, padded.size))[: row.size]
        rows.append(np.sort(windows, axis=1)[:, half])
    return np.stack(rows)
