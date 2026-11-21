_MT_N = 312
_MT_M = 156
_MT_MASK = 0xFFFFFFFFFFFFFFFF


def _mt_seed(seed):
    state = [int(seed) & _MT_MASK]
    for i in range(1, _MT_N):
        prev = state[-1]
        state.append((6364136223846793005 * (prev ^ (prev >> 62)) + i) & _MT_MASK)
    return np.array(state, dtype=np.uint64)


def _mt_mix(upper, lower, far):
    x = (upper & np.uint64(0xFFFFFFFF80000000)) | (lower & np.uint64(0x7FFFFFFF))
    odd = (x & np.uint64(1)) == np.uint64(1)
    return far ^ (x >> np.uint64(1)) ^ np.where(odd, np.uint64(0xB5026F5AA96619E9), np.uint64(0))


def _mt_twist(mt):
    # Word i needs the old word i + 1 and word i + 156: the first 156 read only old words, the next 155 read words
    # already renewed in this pass, and the last one reads the new words 0 and 155.
    k = _MT_N - _MT_M
    mt[:k] = _mt_mix(mt[:k], mt[1:k + 1], mt[_MT_M:])
    mt[k:_MT_N - 1] = _mt_mix(mt[k:_MT_N - 1], mt[k + 1:], mt[:_MT_N - 1 - k])
    mt[_MT_N - 1:] = _mt_mix(mt[_MT_N - 1:], mt[:1], mt[_MT_M - 1:_MT_M])


def _mt_temper(y):
    y = y ^ ((y >> np.uint64(29)) & np.uint64(0x5555555555555555))
    y = y ^ ((y << np.uint64(17)) & np.uint64(0x71D67FFFEDA60000))
    y = y ^ ((y << np.uint64(37)) & np.uint64(0xFFF7EEE000000000))
    return y ^ (y >> np.uint64(43))


class _PortableStream:
    def __init__(self, seed):
        self.mt = _mt_seed(seed)
        self.left = np.empty(0, dtype=np.uint64)

    def words(self, count):
        parts, have = [self.left], self.left.size
        while have < count:
            _mt_twist(self.mt)
            parts.append(_mt_temper(self.mt.copy()))
            have += _MT_N
        words = np.concatenate(parts)
        self.left = words[count:]
        return words[:count]

    def uniform(self, count):
        return (self.words(count) >> np.uint64(11)).astype(np.float64) * 2.0 ** -53


def _portable_log(x):
    m, e = np.frexp(x)
    low = m < 0.70710678118654752440
    m = np.where(low, m * 2, m)
    e = np.where(low, e - 1, e).astype(np.float64)
    s = (m - 1) / (m + 1)
    s2 = s * s
    t = np.full_like(s, 1.0 / 25)
    for k in range(11, -1, -1):
        t = t * s2 + 1.0 / (2 * k + 1)
    return e * 6.93147180369123816490e-01 + (2 * s * t + e * 1.90821492927058770002e-10)


def _portable_normals(stream, n):
    parts, have = [], 0
    while have < n:
        pairs = (n - have + 1) // 2 * 4 // 3 + 16
        u01 = stream.uniform(2 * pairs)
        u = 2 * u01[0::2] - 1
        v = 2 * u01[1::2] - 1
        s = u * u + v * v
        keep = (s < 1) & (s != 0)
        u, v, s = u[keep], v[keep], s[keep]
        f = np.sqrt(-2 * _portable_log(s) / s)
        both = np.empty(2 * s.size)
        both[0::2] = u * f
        both[1::2] = v * f
        parts.append(both)
        have += both.size
    return np.concatenate(parts)[:n] if parts else np.empty(0)


def portable_random(seed, n, kind="normal"):
    stream = _PortableStream(seed)
    n = int(n)
    if kind == "uniform":
        return stream.uniform(n)
    if kind == "tpdf":
        u01 = stream.uniform(2 * n)
        return u01[0::2] - u01[1::2]
    return _portable_normals(stream, n)
