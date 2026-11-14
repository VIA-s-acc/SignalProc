# import: from scipy.io import wavfile
def read_wav(path):
    rate, x = wavfile.read(path)
    if x.dtype == np.uint8:
        y = (x.astype(np.float64) - 128) / 128
    elif x.dtype == np.int16:
        y = x / 32768.0
    elif x.dtype == np.int32:
        y = x / 2147483648.0
    else:
        y = x.astype(np.float64)
    y = y.T if y.ndim == 2 else y[np.newaxis, :]
    return np.ascontiguousarray(y), float(rate)
