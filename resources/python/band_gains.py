# needs: band_index
def band_gains(c, meta, enabled, gains_db):
    names = [item.strip() for item in str(enabled).split(",") if item.strip()]
    keep = [not names or names[0] == "all"] * len(c)
    for name in names:
        if name != "all":
            k = band_index(c, meta, name)
            if k is not None:
                keep[k] = True
    gains = [1.0] * len(c)
    for item in str(gains_db).split(","):
        name, eq, value = item.partition("=")
        if eq:
            k = band_index(c, meta, name)
            if k is not None:
                gains[k] = 10.0 ** (float(value) / 20)
    return [g if k else None for g, k in zip(gains, keep)]
