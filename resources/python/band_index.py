def band_index(c, meta, name):
    name = str(name).strip()
    paths = meta.get("paths") or []
    if name in paths:
        return paths.index(name)
    levels = len(c) - 1
    if name in ("a", "a" + str(levels)):
        return 0
    if len(name) < 2 or name[0] != "d" or not name[1:].isdigit():
        return None
    k = int(name[1:])
    return levels + 1 - k if 1 <= k <= levels else None
