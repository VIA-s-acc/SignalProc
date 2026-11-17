# import: import pywt
def wavelet(name):
    key = "wavelet." + str(name) + "."
    if key + "dec_lo" in D.files:
        return pywt.Wavelet(name, filter_bank=[D[key + part] for part in ("dec_lo", "dec_hi", "rec_lo", "rec_hi")])
    if name in pywt.wavelist(kind="discrete"):
        return pywt.Wavelet(name)
    raise ValueError(NO_WAVELET.format(name=name))
