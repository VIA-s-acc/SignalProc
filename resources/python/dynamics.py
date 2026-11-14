# import: import math
def _dynamics_smoothing(ms, fs):
    samples = ms / 1000 * fs
    return math.exp(-1 / samples) if samples > 0 else 0.0


def _dynamics_peak_db(x):
    return [20 * math.log10(max(float(np.max(np.abs(x[:, n]))), 1e-10)) for n in range(x.shape[1])]


def _dynamics_rms_db(x, coef):
    out, power = [], 0.0
    for n in range(x.shape[1]):
        square = 0.0
        for v in x[:, n]:
            square += v * v
        power = coef * power + (1 - coef) * (square / x.shape[0])
        out.append(10 * math.log10(max(power, 1e-20)))
    return out


def _dynamics_curve(v, threshold, slope, knee):
    over = v - threshold
    if knee > 0 and 2 * abs(over) <= knee:
        return v + (slope - 1) * (over + knee / 2) * (over + knee / 2) / (2 * knee)
    return threshold + over * slope if over > 0 else v


def _dynamics_apply(x, gain_db):
    return x * np.array([10.0 ** (g / 20) for g in gain_db])[np.newaxis, :]


def dynamics(x, fs, kind, threshold, ratio=1.0, knee=0.0, attack_ms=0.0, release_ms=0.0, makeup=0.0, detector="peak", rms_ms=10.0, range_db=0.0):
    attack, release = _dynamics_smoothing(attack_ms, fs), _dynamics_smoothing(release_ms, fs)
    if kind == "gate":
        level, closed = _dynamics_peak_db(x), -range_db
        gain, state = [], 0.0
        for v in level:
            target = closed if v < threshold else 0.0
            coef = attack if target > state else release
            state = coef * state + (1 - coef) * target
            gain.append(state)
        return _dynamics_apply(x, gain)
    level = _dynamics_rms_db(x, _dynamics_smoothing(rms_ms, fs)) if detector == "rms" else _dynamics_peak_db(x)
    slope = 0.0 if kind == "limiter" else 1 / ratio
    gain, state = [], 0.0
    for v in level:
        target = _dynamics_curve(v, threshold, slope, knee) - v
        coef = attack if target < state else release
        state = coef * state + (1 - coef) * target
        gain.append(state + makeup)
    return _dynamics_apply(x, gain)
