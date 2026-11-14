# import: import math
# import: from scipy import fft as sp_fft
def _sst_peak(mu):
    ks, w = math.exp(-0.5 * mu * mu), mu
    for _ in range(200):
        following = mu + ks * w * math.exp(0.5 * (w - mu) * (w - mu) - 0.5 * w * w)
        if abs(following - w) < 1e-15 * mu:
            return following
        w = following
    return w


def _sst_gain(mu):
    return math.sqrt(2.0) * (1 + math.exp(-mu * mu) - 2 * math.exp(-0.75 * mu * mu)) ** -0.5 * math.pi**0.25


def _sst_morlet(w, mu):
    return _sst_gain(mu) * (np.exp(-0.5 * (w - mu) * (w - mu)) - math.exp(-0.5 * mu * mu) * np.exp(-0.5 * w * w))


def _sst_admissibility(mu):
    gain, h = _sst_gain(mu), (mu + 40) / 4000
    node = np.array([0.0950125098376374, 0.2816035507792589, 0.4580167776572274, 0.6178762444026438, 0.7554044083550030, 0.8656312023878318, 0.9445750230732326, 0.9894009349916499])
    weight = np.array([0.1894506104550685, 0.1826034150449236, 0.1691565193950025, 0.1495959888165767, 0.1246289712555339, 0.0951585116824928, 0.0622535239386479, 0.0271524594117541])

    def integrand(w):
        with np.errstate(all="ignore"):
            small = gain * np.exp(-0.5 * (w * w + mu * mu)) * np.expm1(mu * w) / w
            large = gain * (np.exp(-0.5 * (w - mu) * (w - mu)) - np.exp(-0.5 * (w * w + mu * mu))) / w
        return np.where(mu * w < 1, small, large)

    mid, half = ((np.arange(4000) + 0.5) * h)[:, None], 0.5 * h
    terms = weight * half * (integrand(mid - half * node) + integrand(mid + half * node))
    return np.cumsum(terms.ravel())[-1]


def _sst_rows(x, s0, count, voices, mu, up, left):
    j = np.arange(up) - left
    period = 2 * (x.size - 1)
    m = j % period if x.size > 1 else np.zeros(up, dtype=int)
    xh = np.fft.fft(x[np.where(m < x.size, m, period - m)])
    i = np.arange(up)
    xi = np.where(i <= up // 2, i, i - up) * (6.283185307179586 / up)
    for k in range(count):
        scale = s0 * 2.0 ** (k / voices)
        psih = np.where(np.abs(xi) > (mu + 40) / scale, 0.0, _sst_morlet(scale * xi, mu))
        if up % 2 == 0:
            psih[up // 2] /= 2
        buffer = psih * xh
        yield np.fft.ifft(buffer)[left : left + x.size], np.fft.ifft(buffer * (1j * xi))[left : left + x.size]


def _sst_segments(n, s0, count, voices):
    margin = math.ceil(6 * (s0 * 2.0 ** ((count - 1) / voices))) + 1
    length = max(4 * margin, 1 << 15)
    if n <= length:
        up = 1 << (1 + round(math.log2(n)))
        return [(0, n, 0, n, up, up - n - (up - n) // 2)]
    out = []
    for start in range(0, n, length):
        stop = min(n, start + length)
        begin, end = (start - margin if start > margin else 0), min(n, stop + margin)
        out.append((begin, end, start - begin, stop - begin, sp_fft.next_fast_len(end - begin + 2 * margin, real=False), margin))
    return out


def sst(row, fs, lo_hz, hi_hz, voices, mu, ridge):
    lo, hi = lo_hz / fs, hi_hz / fs
    s0 = _sst_peak(mu) / (6.283185307179586 * min(0.5, 1.5 * hi))
    count = math.floor(voices * math.log2(min(0.5, 1.5 * hi) / (lo / 1.5)) + 1e-9) + 1
    weight = math.log(2) / voices
    band, freq, amp = np.zeros(row.size), np.zeros(row.size), np.zeros(row.size)
    for begin, end, keep_from, keep_to, up, left in _sst_segments(row.size, s0, count, voices):
        part = slice(begin + keep_from, begin + keep_to)
        for wx, dwx in _sst_rows(row[begin:end], s0, count, voices, mu, up, left):
            wx, dwx = wx[keep_from:keep_to], dwx[keep_from:keep_to]
            m = np.abs(wx)
            a, b, c, d = dwx.real, dwx.imag, wx.real, wx.imag
            with np.errstate(all="ignore"):
                w = np.abs((b * c - a * d) / ((c * c + d * d) * 6.283185307179586))
            inside = (m > 10 * np.finfo(float).eps) & (w >= lo) & (w <= hi)
            if ridge:
                better = inside & (m > amp[part])
                amp[part] = np.where(better, m, amp[part])
                freq[part] = np.where(better, w, freq[part])
            else:
                band[part] += np.where(inside, c * weight, 0.0)
    if ridge:
        return freq * fs, amp * (2 / _sst_morlet(_sst_peak(mu), mu))
    return band * (2 / _sst_admissibility(mu))
