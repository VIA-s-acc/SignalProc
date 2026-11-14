def real_cepstrum(x):
    rows = []
    for row in x:
        spectrum = np.fft.fft(row)
        peak = np.max(np.abs(spectrum)) if spectrum.size else 0.0
        if not peak > 0:
            rows.append(np.zeros(row.size))
            continue
        rows.append(np.fft.ifft(np.log(np.abs(spectrum) + 1e-12 * peak)).real)
    return np.stack(rows)
