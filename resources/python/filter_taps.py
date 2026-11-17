# needs: wavelet
def filter_taps(spec):
    name, _, role = str(spec).rpartition(".")
    w = wavelet(name)
    taps = {"h0": w.dec_lo, "h1": [-v for v in w.dec_hi], "f0": w.rec_lo, "f1": [-v for v in w.rec_hi]}
    return np.array(taps[role], dtype=np.float64)
