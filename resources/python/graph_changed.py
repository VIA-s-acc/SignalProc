def graph_changed(now, built):
    for key, value in built.items():
        got = now[key]
        if isinstance(value, str) or isinstance(got, str):
            if got != value:
                return True
        elif not np.isclose(got, value, rtol=1e-12, atol=0.0):
            return True
    return False
