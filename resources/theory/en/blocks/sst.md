+++
id = "sst"
title = "Synchrosqueezing — band and ridge"
theory = ["synchrosqueezing", "cwt", "stft"]
source = "src/core/sst.cpp"
function = "band"
test = "tests/core/test_sst.cpp"
views = ["pipeline", "decomposition"]
+++

## Purpose

The continuous wavelet transform smears every component of a signal over many neighbouring scales, so a tone shows up on a scalogram as a wide band. Synchrosqueezing moves every coefficient to the frequency it actually sees — the instantaneous frequency from the derivative of its phase — and adds the coefficients up there. The picture becomes sharp, and a single component can be rebuilt from it.

`sst_band` keeps the coefficients whose instantaneous frequency falls in the band $[lo, hi]$ and rebuilds the signal from them. `sst_ridge` takes the strongest coefficient in the band at every sample and returns its instantaneous frequency in hertz and its amplitude.

## Formulas

The $L^1$-normalised CWT is computed with the FFT; the time derivative is the same inverse FFT with the factor $i\xi$:

$$W(a,b) = \mathcal{F}^{-1}\left\{\hat{x}(\xi)\,\hat{\psi}(a\xi)\right\}(b), \quad \partial_b W(a,b) = \mathcal{F}^{-1}\left\{i\xi\,\hat{x}(\xi)\,\hat{\psi}(a\xi)\right\}(b)$$

The Morlet wavelet in the frequency domain, as in ssqueezepy:

$$\hat{\psi}(\omega) = \sqrt{2}\,c_\mu\,\pi^{1/4}\left(e^{-(\omega-\mu)^2/2} - e^{-\mu^2/2}\,e^{-\omega^2/2}\right)$$

The instantaneous frequency in cycles per sample, wherever $|W(a,b)| > \gamma$:

$$\omega(a,b) = \frac{1}{2\pi}\left|\operatorname{Im}\frac{\partial_b W(a,b)}{W(a,b)}\right|$$

Scales are $a_k = s_0\,2^{k/n_v}$, where $n_v$ is the number of voices per octave. The synchrosqueezed transform adds the coefficients up by frequency:

$$T(f_l, b) = \sum_{k:\ \omega(a_k,b) \approx f_l} W(a_k, b)\,\frac{\ln 2}{n_v}$$

The signal is rebuilt by a sum over all frequencies, and a band by a sum over the frequencies of the band:

$$x(b) \approx \frac{2}{C_\psi}\operatorname{Re}\sum_l T(f_l, b), \quad C_\psi = \int_0^\infty \frac{\hat{\psi}(\xi)}{\xi}\,d\xi$$

The ridge amplitude is $A(b) = 2\,|W(a^*, b)| / \hat{\psi}(\omega_p)$, where $a^*$ is the scale with the largest $|W|$ among the frequencies of the band and $\omega_p \approx \mu$ is the peak of $\hat{\psi}$.

## Algorithm and cost

1. The scales cover the band with a margin of one and a half times: from $\omega_p / (2\pi \cdot 1.5\,hi)$ to $1.5\,\omega_p / (2\pi\,lo)$, with frequencies as fractions of the sample rate.
2. The signal is extended by reflection to a power of two, as in ssqueezepy. A long signal is cut into pieces with a margin of $6\,a_{\max}$ samples on each side, so memory does not grow with the length of the recording.
3. One forward FFT, then two inverse FFTs per scale: $W$ and $\partial_b W$. Scales are computed in batches on several threads.
4. Every coefficient gets its instantaneous frequency. `sst_band` adds up the real parts of the coefficients with a frequency in the band and multiplies by $2/C_\psi$; `sst_ridge` remembers the strongest coefficient.

Time $O(n_v \log_2(hi/lo)\,N \log N)$: a pair of FFTs per scale. Memory is a few buffers the length of a piece, not of the whole signal.

## With this block's numbers

Band: from {{block.lo}} to {{block.hi}} Hz; voices per octave: {{block.voices}}; $\mu$ = {{block.mu}}.

## Checks

- The synchrosqueezed transform matches ssqueezepy 0.6.6 (`ssq_cwt`, Morlet wavelet, reflect padding) bin for bin to $10^{-10}$ on five signals: tones, a chirp, a mixture and a three-sample signal; log and linear bins, $\mu$ = 5, 6 and 13.4. The CWT itself matches to $10^{-12}$.
- $C_\psi$ matches the exact integral to $10^{-13}$. ssqueezepy computes it with trapezoids and is off by about $10^{-7}$, so the inverse is compared after correcting for that ratio.
- A band pulls one of two tones out with an error below 2 %, and the whole band rebuilds the signal just as well.
- The ridge follows a chirp with a frequency error below 1 % and an amplitude error below 2 %. Of two tones it takes the stronger one, and in a narrowed band the weaker one.
- The transform cut into pieces gives the same picture as the whole one: visible cells differ by less than 0.1 dB.
- NaN, infinite and above-$10^{150}$ samples, an empty channel, a wrong band and a signal without a sample rate are refused.

## Links

- §11.7 — synchrosqueezing.
- §11.6 — the continuous wavelet transform it is built on.
- §11.9 — STFT: another road to a time-frequency picture.

## Implementation

The instantaneous frequency of a coefficient, and the band put together from the coefficients whose frequency falls in it.

```cpp
double frequency(C wx, C dwx) {
    const double a = dwx.real(), b = dwx.imag(), c = wx.real(), d = wx.imag();
    return std::abs((b * c - a * d) / ((c * c + d * d) * kTwoPi));
}
```

```cpp
                for (std::size_t j = 0; j < wx.size(); ++j)
                    for (std::size_t t = from; t < to; ++t) {
                        if (!(std::abs(wx[j][t]) > kGamma)) continue;
                        const double w = frequency(wx[j][t], dwx[j][t]);
                        if (w >= lo && w <= hi) out[seg.from + t] += wx[j][t].real() * weight;
                    }
```
