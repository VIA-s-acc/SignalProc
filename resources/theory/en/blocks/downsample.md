+++
id = "downsample"
title = "Downsampling by M"
theory = ["multirate-down-up", "multirate-anti-alias"]
source = "src/core/multirate.cpp"
function = "decimate"
test = "tests/core/test_multirate.cpp"
views = ["signal"]
+++

## Purpose

Lower the sampling rate by a factor of $M$, keeping every $M$-th sample; beforehand a lowpass filter removes everything above $\pi/M$, otherwise it folds into the signal band.

## Formulas

$${\downarrow}M:\quad y[n] = x[Mn], \qquad Y(e^{i\omega}) = \frac{1}{M}\sum_{k=0}^{M-1} X\big(e^{i(\omega - 2\pi k)/M}\big)$$

The spectrum is stretched by a factor of $M$ and the $M$ copies overlap — aliasing, unless $X$ is band-limited to $\lvert\omega\rvert < \pi/M$. Decimation is a lowpass filter with cutoff $\pi/M$ and gain $1$, followed by ${\downarrow}M$:

$$y[k] = (h * x)[kM], \qquad h[m] = \frac{\sin(\pi m / M)}{\pi m}\, w[m], \qquad \sum_m h[m] = 1$$

The window $w$ is Kaiser. Filter length from the attenuation $A$ dB and the transition width $\Delta\omega = \pi\tau / M$ ($\tau = 0.1$ by default):

$$L \approx \frac{A - 8}{2.285\,\Delta\omega} + 1$$

## Algorithm and cost

1. The factor $M = 1$ is the identity; $M = 0$ is an error.
2. Without anti-aliasing the samples $x[kM]$ are taken as is: frequencies above $\pi/M$ fold to $\lvert\omega_0 M - 2\pi k\rvert$.
3. With anti-aliasing an odd-length Kaiser FIR filter is built; its delay $(L-1)/2$ is compensated by shifting the selected samples.
4. Full convolution (§1.1), then every $M$-th sample is selected.

Output length $\lceil N/M \rceil$. Cost $\approx N L$ multiplications (long factors go through the FFT): the implementation computes the whole convolution and discards the excess; the polyphase form (§3.4) would cost $N L / M$.

## With this block's numbers

$M = {{block.m}}$: the sampling rate drops by a factor of ${{block.m}}$, and the signal band is limited to $\omega < \pi / {{block.m}}$.

Anti-aliasing: {{block.anti_alias}}; filter taps $L = {{block.taps}}$; stopband attenuation (dB): {{block.attenuation_db}}.

Output: $y[k] = (h * x)[k \cdot {{block.m}} + ({{block.taps}} - 1)/2]$.

## Checks

It would be worth checking that a tone in the passband passes without amplitude or phase distortion, that a tone above $\pi/M$ is suppressed at least by the specified attenuation, and that without the filter it lands at the frequency $\lvert\omega_0 M - 2\pi k\rvert$ — these checks live in `tests/core/test_multirate.cpp`; they are not on screen.

## Links

- §3.1 — downsampling and upsampling: aliasing and mirror images.
- §3.2 — decimator and interpolator for arbitrary $M$, filter specification.
- §3.4 — polyphase decomposition: only the kept samples are computed.
- §9.3 — the Kaiser window: where the formulas for $L$ and $\beta$ come from.
- §4.2 — in a two-channel bank aliasing is acceptable; synthesis cancels it.
- §11.1 — the DWT uses ${\downarrow}2$ without anti-aliasing.

## Implementation

Sample selection with or without the filter: every $M$-th sample of the convolution, shifted by the filter delay.

```cpp
if (!options.anti_alias) {
    for (std::size_t k = 0; k < out_len; ++k) y[k] = x[k * m];
    return y;
}
const auto h = anti_alias_filter(m, options);
const auto filtered = convolve(Laurent{{x.begin(), x.end()}, 0}, h);
const std::size_t d = filter_delay(h, options);
for (std::size_t k = 0; k < out_len; ++k) y[k] = filtered.coeffs[k * m + d];
return y;
```
