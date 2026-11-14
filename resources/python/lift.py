def _lifting_clamped(v, i):
    return v[np.clip(i, 0, v.size - 1)]


def _lifting_even_at(x, i):
    n = x.size
    i = np.where(i >= n, 2 * (n - 1) - i, i)
    i = np.where(i < 0, -i, i)
    return x[np.clip(i, 0, n - 1)]


def _lifting_round(v):
    q = np.rint(v)
    tie = np.abs(v - np.trunc(v)) == 0.5
    return np.where(tie, np.trunc(v) + np.sign(v), q)


def _lifting_53(x, integer):
    s, odd = x[0::2].copy(), np.arange(1, x.size, 2)
    if not odd.size:
        return s, x[1::2].copy()
    if integer:
        d = x[odd] - (x[odd - 1] + _lifting_even_at(x, odd + 1)) // 2
        i = np.arange(s.size)
        s = s + (_lifting_clamped(d, i - 1) + _lifting_clamped(d, i) + 2) // 4
    else:
        d = x[odd] - 0.5 * (x[odd - 1] + _lifting_even_at(x, odd + 1))
        i = np.arange(s.size)
        s = s + 0.25 * (_lifting_clamped(d, i - 1) + _lifting_clamped(d, i))
    return s, d


def _lifting_un53(s, d, integer):
    n = s.size + d.size
    x = np.zeros(n, dtype=s.dtype)
    if not d.size:
        x[0::2] = s
        return x
    i = np.arange(s.size)
    x[0::2] = s - ((_lifting_clamped(d, i - 1) + _lifting_clamped(d, i) + 2) // 4 if integer else 0.25 * (_lifting_clamped(d, i - 1) + _lifting_clamped(d, i)))
    k = np.arange(d.size)
    right = np.where(2 * k + 2 < n, x[np.minimum(2 * k + 2, n - 1)], x[2 * k])
    x[1::2] = d + ((x[2 * k] + right) // 2 if integer else 0.5 * (x[2 * k] + right))
    return x


def _lifting_97(x, c):
    alpha, beta, gamma, delta, zeta = c
    s, d = x[0::2].copy(), x[1::2].copy()
    if not d.size:
        return s, d
    i, j = np.arange(d.size), np.arange(s.size)
    d = d + alpha * (s[i] + _lifting_clamped(s, i + 1))
    s = s + beta * (_lifting_clamped(d, j - 1) + _lifting_clamped(d, j))
    d = d + gamma * (s[i] + _lifting_clamped(s, i + 1))
    s = s + delta * (_lifting_clamped(d, j - 1) + _lifting_clamped(d, j))
    return s * zeta, d / zeta


def _lifting_un97(s, d, c):
    alpha, beta, gamma, delta, zeta = c
    if not d.size:
        return s
    s, d = s / zeta, d * zeta
    i, j = np.arange(d.size), np.arange(s.size)
    s = s - delta * (_lifting_clamped(d, j - 1) + _lifting_clamped(d, j))
    d = d - gamma * (s[i] + _lifting_clamped(s, i + 1))
    s = s - beta * (_lifting_clamped(d, j - 1) + _lifting_clamped(d, j))
    d = d - alpha * (s[i] + _lifting_clamped(s, i + 1))
    x = np.zeros(s.size + d.size)
    x[0::2], x[1::2] = s, d
    return x


def lift(x, levels, kind, constants=None):
    channels = []
    for row in x:
        approx = np.clip(_lifting_round(row * 32768.0), -2147483647, 2147483647).astype(np.int64) if kind == "lift53i" else np.array(row, dtype=np.float64)
        details = []
        for _ in range(levels):
            approx, d = _lifting_97(approx, constants) if kind == "lift97" else _lifting_53(approx, kind == "lift53i")
            details.append(d)
        channels.append([approx] + details[::-1])
    return [np.stack([ch[b] for ch in channels]).astype(np.float64) for b in range(levels + 1)]


def unlift(c, kind, constants=None):
    rows = []
    for ch in range(c[0].shape[0]):
        if kind == "lift53i":
            y = _lifting_round(c[0][ch]).astype(np.int64)
            for band in c[1:]:
                y = _lifting_un53(y, _lifting_round(band[ch]).astype(np.int64), True)
            rows.append(y / 32768.0)
            continue
        y = np.array(c[0][ch], dtype=np.float64)
        for band in c[1:]:
            y = _lifting_un97(y, band[ch], constants) if kind == "lift97" else _lifting_un53(y, band[ch], False)
        rows.append(y)
    return np.stack(rows)
