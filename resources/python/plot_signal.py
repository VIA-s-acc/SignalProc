# import: import matplotlib.pyplot as plt
def plot_signal(x, fs, title):
    t = np.arange(x.shape[1]) / fs
    plt.figure(figsize=(9, 2.5))
    for ch, row in enumerate(x):
        plt.plot(t, row, linewidth=0.8, label=f"ch{ch}")
    plt.title(title)
    plt.xlabel("t, s")
    if x.shape[0] > 1:
        plt.legend(loc="upper right")
    plt.show()
