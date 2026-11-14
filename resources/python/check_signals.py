# import: import math
def check_signals(ref, test, kind, tol, delay):
    shift = 0
    if delay == "auto":
        if ref.shape[0] and test.shape[0]:
            a0, b0 = ref[0], test[0]
            n0 = min(a0.size, b0.size)
            last = min(b0.size - a0.size, 4096) if b0.size > a0.size else 0
            best = math.inf
            for s in range(last + 1 if n0 else 0):
                e = float(np.max(np.abs(a0[:n0] - b0[s : s + n0])))
                if e < best:
                    best, shift = e, s
    else:
        shift = int(delay)
    worst, rms, snr, covered = 0.0, 0.0, math.inf, True
    for ch in range(min(ref.shape[0], test.shape[0])):
        n = min(ref.shape[1], test.shape[1] - shift) if test.shape[1] > shift else 0
        if n < ref.shape[1]:
            covered = False
        a, b = ref[ch, :n], test[ch, shift : shift + n]
        d = a - b
        noise = math.fsum(d * d)
        worst = max(worst, float(np.max(np.abs(d))) if n else 0.0)
        rms = max(rms, math.sqrt(noise / n) if n else 0.0)
        snr = min(snr, math.inf if noise == 0 else 10 * math.log10(math.fsum(a * a) / noise))
    fields = {"max_abs": worst, "rms": rms, "snr_db": snr}
    if delay != "0":
        fields["delay"] = float(shift)
    passed = snr >= tol if kind == "snr" else worst <= tol
    if ref.shape[0] != test.shape[0] or not covered:
        passed = False
    return fields, passed
