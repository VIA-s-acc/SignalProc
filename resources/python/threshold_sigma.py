def threshold_sigma(value, norm):
    if str(value) == "mad_d1":
        return None
    return float(value) * (np.sqrt(0.5) if norm else 1.0)
