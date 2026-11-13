# Magnitude response of {{w}} at 0.75π

Find the gain of the lowpass `{{w}}.h0` at the frequency $\omega = 0.75\pi$ in decibels (a negative number). Tolerance 0.2 dB.

## Hints {#hints}

1. On the «Filters» tab choose the wavelet `{{w}}`, the filter $h_0$, and put the cursor at $\omega/\pi = 0.75$. The plot shows $20\log_{10}|H_0|$, see {#freq-magnitude}.
2. You can also compute it directly: $H_0(e^{j\omega}) = \sum_k h_0[k]\,e^{-j\omega k}$ at $\omega = 0.75\pi$.
3. Compare neighbouring orders: a higher order $N$ of $\text{db}N$ gives a deeper drop in the upper half of the band, though not necessarily at every frequency.

## Solution {#solution}

The gain of `{{w}}.h0` at $\omega = 0.75\pi$ is {{expected}} dB. A graph gives the same number: a tone at this frequency (for $f_s = 8000$ Hz that is 3000 Hz) through `convolve(filter={{w}}.h0)` loses exactly that much. Daubechies filters are normalised to gain $\sqrt2$ (about 3.01 dB) at zero frequency, so stopband levels are counted from there; more on the responses of wavelet filters in {#freq-phi-psi-spectra}.
