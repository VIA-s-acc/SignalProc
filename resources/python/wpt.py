# import: import math
def _wpt_cost(v, norm2):
    total = 0.0
    for x in v:
        if x == 0:
            continue
        if norm2 > 0:
            p = x * x / norm2
            total -= p * math.log(p)
    return total


def _wpt_node(tree, path):
    return tree.data if path == "" else tree[path].data


def wpt(x, wavelet, mode, levels, basis):
    trees = [pywt.WaveletPacket(row, wavelet, mode=mode, maxlevel=levels) for row in x]
    if basis == "best":
        norm2 = []
        for row in x:
            s = 0.0
            for v in row:
                s += v * v
            norm2.append(s)
        paths = {j: [format(n, f"0{j}b").replace("0", "a").replace("1", "d") if j else "" for n in range(2 ** j)] for j in range(levels + 1)}
        best, split = {}, {}
        for j in range(levels, -1, -1):
            for n, path in enumerate(paths[j]):
                own = 0.0
                for t, tree in enumerate(trees):
                    own += _wpt_cost(_wpt_node(tree, path), norm2[t])
                best[path], split[path] = own, False
                if j < levels:
                    children = best[path + "a"] + best[path + "d"]
                    if children < own:
                        best[path], split[path] = children, True
        leaves, stack = [], [""]
        while stack:
            path = stack.pop()
            if split[path]:
                stack += [path + "d", path + "a"]
            else:
                leaves.append(path)
    else:
        return [np.stack([_wpt_node(t, p) for t in trees]) for p in basis], list(basis)
    deepest = max(len(p) for p in leaves)

    def freq(path):
        k = int(path.replace("a", "0").replace("d", "1"), 2) if path else 0
        s = 1
        while s < 64:
            k ^= k >> s
            s <<= 1
        return k << (deepest - len(path))

    leaves.sort(key=freq)
    return [np.stack([_wpt_node(t, p) for t in trees]) for p in leaves], leaves


def _wpt_rebuild(path, leaves, lengths, wavelet, mode):
    if path in leaves:
        return leaves[path]
    a = _wpt_rebuild(path + "a", leaves, lengths, wavelet, mode)
    d = _wpt_rebuild(path + "d", leaves, lengths, wavelet, mode)
    return pywt.idwt(a, d, wavelet, mode)[: lengths[len(path)]]


def iwpt(c, paths, wavelet, mode, n):
    w = wavelet if isinstance(wavelet, pywt.Wavelet) else pywt.Wavelet(wavelet)
    lengths = [n]
    for _ in range(max(len(p) for p in paths)):
        lengths.append(pywt.dwt_coeff_len(lengths[-1], w.dec_len, mode))
    rows = []
    for ch in range(c[0].shape[0]):
        leaves = {path: band[ch] for band, path in zip(c, paths)}
        rows.append(_wpt_rebuild("", leaves, lengths, w, mode)[:n])
    return np.stack(rows)
