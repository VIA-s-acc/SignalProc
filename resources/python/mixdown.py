def mixdown(x):
    total = np.zeros(x.shape[1])
    for row in x:
        total = total + row
    return (total / x.shape[0])[np.newaxis, :]
