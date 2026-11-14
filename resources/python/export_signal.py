def _export_signal_number(v):
    return f"{v:.17g}" if np.isfinite(v) else "null"


def export_signal(path, x, fs, fmt):
    path = Path(path)
    x = np.atleast_2d(np.asarray(x, dtype=np.float64))
    if fmt == "npy":
        with open(path, "wb") as f:
            np.save(f, x[0] if x.shape[0] == 1 else x)
        return {"path": str(path), "bytes": float(path.stat().st_size)}
    if fmt == "csv":
        rows = max((r.size for r in x), default=0)
        text = "n" + "".join(f",ch{c}" for c in range(x.shape[0])) + "\n"
        for k in range(rows):
            text += str(k) + "".join("," + (f"{r[k]:.17g}" if k < r.size else "") for r in x) + "\n"
    else:
        text = '{"rate":' + _export_signal_number(fs) + ',"channels":[' + ",".join("[" + ",".join(_export_signal_number(v) for v in r) + "]" for r in x) + "]}\n"
    data = text.encode()
    path.write_bytes(data)
    return {"path": str(path), "bytes": float(len(data))}
