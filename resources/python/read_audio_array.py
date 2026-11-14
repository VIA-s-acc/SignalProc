def read_audio_array(path, key):
    if key is None:
        return np.atleast_2d(np.load(path, allow_pickle=False)).astype(np.float64)
    return np.atleast_2d(np.load(path, allow_pickle=False)[key]).astype(np.float64)
