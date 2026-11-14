# import: import math
# import: from scipy import signal
def biquad(x, kind, f0, q, gain_db, fs):
    gain = 10.0 ** (gain_db / 40)
    w0 = 2 * math.pi * f0 / fs
    cw, alpha = math.cos(w0), math.sin(w0) / (2 * q)
    shelf = 2 * math.sqrt(gain) * alpha
    a0, a1, a2 = 1 + alpha, -2 * cw, 1 - alpha
    if kind == "lowpass":
        b = [(1 - cw) / 2, 1 - cw, (1 - cw) / 2]
    elif kind == "highpass":
        b = [(1 + cw) / 2, -(1 + cw), (1 + cw) / 2]
    elif kind == "bandpass":
        b = [alpha, 0.0, -alpha]
    elif kind == "notch":
        b = [1.0, -2 * cw, 1.0]
    elif kind == "allpass":
        b = [1 - alpha, -2 * cw, 1 + alpha]
    elif kind == "peaking":
        b = [1 + alpha * gain, -2 * cw, 1 - alpha * gain]
        a0, a2 = 1 + alpha / gain, 1 - alpha / gain
    elif kind == "lowshelf":
        b = [gain * ((gain + 1) - (gain - 1) * cw + shelf), 2 * gain * ((gain - 1) - (gain + 1) * cw), gain * ((gain + 1) - (gain - 1) * cw - shelf)]
        a0, a1, a2 = (gain + 1) + (gain - 1) * cw + shelf, -2 * ((gain - 1) + (gain + 1) * cw), (gain + 1) + (gain - 1) * cw - shelf
    else:
        b = [gain * ((gain + 1) + (gain - 1) * cw + shelf), -2 * gain * ((gain - 1) + (gain + 1) * cw), gain * ((gain + 1) + (gain - 1) * cw - shelf)]
        a0, a1, a2 = (gain + 1) - (gain - 1) * cw + shelf, 2 * ((gain - 1) - (gain + 1) * cw), (gain + 1) - (gain - 1) * cw - shelf
    return signal.lfilter([b[0] / a0, b[1] / a0, b[2] / a0], [1.0, a1 / a0, a2 / a0], x, axis=1)
