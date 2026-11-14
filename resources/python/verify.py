_verify_rows = []


def verify(what, got, ref, tol, origin):
    got = np.asarray(got, dtype=np.float64)
    ref = np.asarray(ref, dtype=np.float64)
    if got.size == ref.size:
        got = got.reshape(ref.shape)
    if got.shape != ref.shape:
        _verify_rows.append((what, None, tol, origin, False))
        return
    same = (got == ref) | (np.isnan(got) & np.isnan(ref))
    diff = np.where(same, 0.0, np.abs(got - ref))
    err = float(np.max(diff)) if diff.size else 0.0
    finite = np.abs(ref[np.isfinite(ref)])
    scale = max(1.0, float(np.max(finite))) if finite.size else 1.0
    _verify_rows.append((what, err, tol, origin, bool(err <= tol * scale)))


def verify_text(what, got, ref):
    ok = str(got) == str(ref)
    _verify_rows.append((what, 0.0 if ok else None, 0.0, "", ok))


def report(labels):
    width = max([len(labels[0])] + [len(r[0]) for r in _verify_rows])
    print(f"{labels[0]:<{width}}  {labels[1]:>22}  {labels[2]:>8}  {labels[3]}")
    for what, err, tol, origin, ok in _verify_rows:
        shown = "—" if err is None else f"{err:.3e}"
        print(f"{what:<{width}}  {shown:>22}  {tol:>8.0e}  {origin:<12} {'✓' if ok else '✗'}")
    return all(r[4] for r in _verify_rows)
