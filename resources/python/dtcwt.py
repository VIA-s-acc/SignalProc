def _dtcwt_gather(x, lo, hi):
    r = x.size
    m = np.arange(lo, hi) % (2 * r)
    return x[np.where(m < r, m, 2 * r - 1 - m)]


def _dtcwt_valid(x, h):
    return np.convolve(x, h, "valid") if x.size >= h.size else np.zeros(0)


def _dtcwt_colfilter(x, h):
    m2 = h.size // 2
    return _dtcwt_valid(_dtcwt_gather(x, -m2, x.size + m2), h)


def _dtcwt_coldfilt(x, ha, hb):
    r, m = x.size, ha.size
    ext = _dtcwt_gather(x, -m, r + m)
    count = len(range(5, r + 2 * m - 2, 4))
    pick = lambda first: ext[first + 4 * np.arange(count)]
    a, b = _dtcwt_valid(pick(4), ha[0::2]), _dtcwt_valid(pick(2), ha[1::2])
    c, d = _dtcwt_valid(pick(5), hb[0::2]), _dtcwt_valid(pick(3), hb[1::2])
    y = np.zeros(r // 2)
    same = float(np.dot(ha[: min(ha.size, hb.size)], hb[: min(ha.size, hb.size)])) > 0
    k = np.arange(a.size)
    y[2 * k + (0 if same else 1)] = a + b
    y[2 * k + (1 if same else 0)] = c + d
    return y


def _dtcwt_colifilt(x, ha, hb):
    r, m = x.size, ha.size
    m2 = m // 2
    y = np.zeros(2 * r)
    if not np.any(x != 0):
        return y
    ext = _dtcwt_gather(x, -m2, r + m2)
    same = float(np.dot(ha[: min(ha.size, hb.size)], hb[: min(ha.size, hb.size)])) > 0
    start, stop = (3, r + m) if m2 % 2 == 0 else (2, r + m - 1)
    count = len(range(start, stop, 2))
    ta, tb = (start, start - 1) if same else (start - 1, start)
    pick = lambda first: ext[first + 2 * np.arange(count)]
    hao, hae, hbo, hbe = ha[0::2], ha[1::2], hb[0::2], hb[1::2]
    if m2 % 2 == 0:
        p = [_dtcwt_valid(pick(tb - 2), hae), _dtcwt_valid(pick(ta - 2), hbe), _dtcwt_valid(pick(tb), hao), _dtcwt_valid(pick(ta), hbo)]
    else:
        p = [_dtcwt_valid(pick(tb), hao), _dtcwt_valid(pick(ta), hbo), _dtcwt_valid(pick(tb), hae), _dtcwt_valid(pick(ta), hbe)]
    for k in range(p[0].size):
        if 4 * k + 3 >= y.size:
            break
        y[4 * k : 4 * k + 4] = [p[0][k], p[1][k], p[2][k], p[3][k]]
    return y


def _dtcwt_filters(D, biort, qshift):
    f = lambda name, part: np.asarray(D[f"dtcwt.{name}.{part}"], dtype=np.float64)
    return {k: f(biort, k) for k in ("h0o", "g0o", "h1o", "g1o")}, {k: f(qshift, k) for k in ("h0a", "h0b", "g0a", "g0b", "h1a", "h1b", "g1a", "g1b")}


def dtcwt(x, levels, biort, qshift, D):
    b, q = _dtcwt_filters(D, biort, qshift)
    channels = []
    for row in x:
        row = np.concatenate([row, row[-1:]]) if row.size % 2 else np.array(row, dtype=np.float64)
        hp = []
        lo = row
        if levels:
            hi = _dtcwt_colfilter(row, b["h1o"])
            hp.append(hi[: 2 * (hi.size // 2)])
            lo = _dtcwt_colfilter(row, b["h0o"])
            for _ in range(1, levels):
                if lo.size % 4:
                    lo = np.concatenate([lo[:1], lo, lo[-1:]])
                hi = _dtcwt_coldfilt(lo, q["h1b"], q["h1a"])
                hp.append(hi[: 2 * (hi.size // 2)])
                lo = _dtcwt_coldfilt(lo, q["h0b"], q["h0a"])
        channels.append([lo] + hp[::-1])
    return [np.stack([ch[k] for ch in channels]) for k in range(len(channels[0]))]


def idtcwt(c, biort, qshift, D, n):
    b, q = _dtcwt_filters(D, biort, qshift)
    rows = []
    for ch in range(c[0].shape[0]):
        lo = c[0][ch]
        hp = [band[ch] for band in c[1:]][::-1]
        for level in range(len(hp) - 1, 0, -1):
            low = _dtcwt_colifilt(lo, q["g0b"], q["g0a"]) + _dtcwt_colifilt(hp[level], q["g1b"], q["g1a"])
            if low.size != hp[level - 1].size:
                low = low[1:-1]
            lo = low
        z = lo if not hp else _dtcwt_colfilter(lo, b["g0o"]) + _dtcwt_colfilter(hp[0], b["g1o"])
        rows.append(z[:n])
    return np.stack(rows)
