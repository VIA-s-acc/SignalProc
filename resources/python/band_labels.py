def band_labels(c, meta):
    if meta["transform"] == "wpt":
        return [path if path else "x" for path in meta["paths"]]
    levels = len(c) - 1
    return ["a" + str(levels)] + ["d" + str(levels + 1 - b) for b in range(1, levels + 1)]
