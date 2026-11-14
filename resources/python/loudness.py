# import: import math
def loudness(x, window, hop):
    frames = x.shape[1]
    length = min(window, frames)
    count, loudest, total = 0, 0.0, 0.0
    start = 0
    while frames > 0 and start + length <= frames:
        power = 0.0
        for row in x:
            for v in row[start : start + length]:
                power += v * v
        power /= length * x.shape[0]
        loudest = max(loudest, power)
        total += power
        start += hop
        count += 1
    db = lambda p: max(-200.0, 10 * math.log10(p)) if p > 0 else -200.0
    return {"windows": float(count), "max_dbfs": db(loudest), "mean_dbfs": db(total / count if count else 0.0)}
