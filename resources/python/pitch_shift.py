# needs: time_stretch
# import: import math
# import: from scipy import signal
def pitch_shift(x, semitones, method, n, hop):
    rate = 2.0 ** (-semitones / 12)
    out = np.zeros_like(x)
    if x.shape[1] == 0:
        return out
    for ch, stretched in enumerate(time_stretch(x, rate, method, n, hop)):
        if stretched.size and rate != 1.0:
            stretched = signal.resample(stretched, math.ceil(stretched.size * rate))
        out[ch, : min(x.shape[1], stretched.size)] = stretched[: x.shape[1]]
    return out
