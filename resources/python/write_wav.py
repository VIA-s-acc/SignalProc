# import: import struct
def write_wav(path, x, fs, bits, dither):
    x = np.atleast_2d(np.asarray(x, dtype=np.float64))
    channels, frames = x.shape
    clipped = 0
    if bits == "f32":
        tag, width = 3, 4
        data = np.ascontiguousarray(x.T).astype("<f4").tobytes()
    else:
        width = int(bits) // 8
        scale = 2.0 ** (int(bits) - 1)
        v = x.T * scale
        if dither is not None:
            v = v + np.asarray(dither).reshape(frames, channels)
        q = np.rint(v)
        tie = np.abs(v - np.trunc(v)) == 0.5
        q = np.where(tie, np.trunc(v) + np.sign(v), q)
        clipped = int(np.count_nonzero((q < -scale) | (q > scale - 1)))
        q = np.clip(q, -scale, scale - 1).astype("<i8")
        tag = 1
        data = np.ascontiguousarray(q.astype("<i4")).view(np.uint8).reshape(-1, 4)[:, :width].tobytes()
    rate = int(fs)
    fmt = struct.pack("<HHIIHH", tag, channels, rate, rate * channels * width, channels * width, 8 * width)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    Path(path).write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)
    return clipped
