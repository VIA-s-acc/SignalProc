# import: from scipy import signal
def speed(x, rate):
    n = round(x.shape[1] * (1 / rate))
    if x.shape[1] == 0 or n == 0:
        return np.zeros((x.shape[0], 0))
    return signal.resample(x, n, axis=1)
