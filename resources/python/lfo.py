def lfo(shape, t):
    if shape == "sine":
        return np.sin(t)
    wrapped = np.fmod(t, 2 * np.pi)
    if shape == "square":
        return np.where(wrapped < np.pi, 1.0, -1.0)
    return np.where(wrapped < np.pi, wrapped / (np.pi * 0.5) - 1, (np.pi * 1.5 - wrapped) / (np.pi * 0.5))
