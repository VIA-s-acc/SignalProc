# import: import math
def reverb(x, fs, room, damping, wet, dry):
    feedback, damp = 0.28 * room + 0.7, 0.4 * damping
    length = lambda n: int(max(1.0, math.floor(n * fs / 44100 + 0.5)))
    rows = []
    for row in x:
        acc = np.zeros(row.size)
        for n in (1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617):
            line, at, store = [0.0] * length(n), 0, 0.0
            for k in range(row.size):
                out = line[at]
                store = out * (1 - damp) + store * damp
                line[at] = 0.015 * row[k] + store * feedback
                at = 0 if at + 1 == len(line) else at + 1
                acc[k] += out
        for n in (556, 441, 341, 225):
            line, at = [0.0] * length(n), 0
            for k in range(row.size):
                held = line[at]
                line[at] = acc[k] + held * 0.5
                acc[k] = held - acc[k]
                at = 0 if at + 1 == len(line) else at + 1
        rows.append(dry * row + wet * 3 * acc)
    return np.stack(rows)
