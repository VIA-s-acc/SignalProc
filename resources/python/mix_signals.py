# needs: pairwise, padded
def mix_signals(a, b, ga, gb, fade, start, length):
    def combine(x, y):
        n = max(x.size, y.size)
        u, v = ga * padded(x, n), gb * padded(y, n)
        if not fade:
            return u + v
        t = np.arange(n, dtype=np.float64) - start
        w = np.where(t < 0, 0.0, np.minimum(1.0, t / length) if length > 0 else 1.0)
        return (1 - w) * u + w * v

    return pairwise(a, b, combine)
