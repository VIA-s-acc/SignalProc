# needs: samples_count
def as_samples(value, fs):
    if isinstance(value, str) and value.endswith("s"):
        return samples_count(float(value[:-1]), fs)
    return int(value)
