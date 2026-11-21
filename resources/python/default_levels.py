# import: import pywt
def default_levels(n, w):
    f = pywt.Wavelet(w).dec_len if isinstance(w, str) else w.dec_len
    top = 0
    if f >= 2 and n >= f - 1:
        while (f - 1) << (top + 1) <= n:
            top += 1
    return top - 3 if top > 4 else 1
