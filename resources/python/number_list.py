def number_list(text, nyquist=None):
    text = str(text)
    if nyquist is not None:
        text = text.replace("nyquist", repr(float(nyquist)))
    return [float(token) for token in text.replace(",", " ").split()]
