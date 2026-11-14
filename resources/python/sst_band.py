# needs: sst
def sst_band(x, fs, lo, hi, voices, mu):
    return np.stack([sst(row, fs, lo, hi, voices, mu, False) for row in x])
