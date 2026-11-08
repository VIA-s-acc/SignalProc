+++
id = "restoration"
title = "Signal restoration"
theory = ["audio-denoising", "stft", "toeplitz-circulant"]
source = "src/core/restoration.cpp"
function = "spectral_subtract"
test = "tests/core/test_restoration.cpp"
views = ["under_the_hood"]
+++

## Purpose

Three classical ways to remove noise or blur without wavelets: `wiener` is a local Wiener filter in time, like `scipy.signal.wiener`; `spectral_subtract` subtracts an estimated noise spectrum after Berouti; `deconvolve` is a Wiener deconvolution of a known kernel in the frequency domain.

## Formulas

The local mean and variance over an odd window of length $K$, $h = (K-1)/2$; beyond the signal edges there are zeros, and the divisor is always $K$:

$$m_n = \frac1K\sum_{j=-h}^{h} x[n+j], \qquad v_n = \frac1K\sum_{j=-h}^{h} x[n+j]^2 - m_n^2$$

$$\hat s[n] = m_n + \Big(1 - \frac{\nu}{v_n}\Big)\big(x[n] - m_n\big) \quad (v_n \ge \nu), \qquad \hat s[n] = m_n \quad (v_n < \nu)$$

$\nu$ is the given noise variance or, if it is 0, the mean of $v_n$ over the whole signal.

Spectral subtraction: an STFT with a periodic Hann window of length $N$, step $R = N/4$, $N/2$ zeros on both sides and the scale $1/\sum w$, as in `scipy.signal.stft`. The noise power $\hat P_k$ is the mean of $\lvert Z_m(k)\rvert^2$ over the frames of the noise segment; the phase of $X$ is kept:

$$\lvert\hat S_m(k)\rvert^2 = \max\big(\lvert X_m(k)\rvert^2 - \alpha\hat P_k,\ \beta\hat P_k\big), \qquad \hat S_m(k) = X_m(k)\,\frac{\lvert\hat S_m(k)\rvert}{\lvert X_m(k)\rvert}$$

The inverse is overlap-add with the same window, divided by $\sum_m w[n - mR]^2$ as in `scipy.signal.istft`, and cut to the length of $x$.

Deconvolution is circular: the DFT length $N$ is the length of $y$, and the kernel $h$ is zero-padded:

$$\hat X_k = \frac{Y_k\,H_k^{*}}{\lvert H_k\rvert^2 + 10^{-\mathrm{SNR}/10}}, \qquad \hat x[n] = \mathrm{Re}\,\frac1N\sum_{k=0}^{N-1}\hat X_k\,e^{2\pi i k n/N}$$

The regulariser $10^{-\mathrm{SNR}/10}$ is a constant noise-to-signal ratio per bin; it is scaled neither by the signal level nor by the kernel gain: with a kernel of unit gain at zero frequency, SNR reads as a power ratio.

## Algorithm and cost

1. `wiener`: direct sums over the window for each sample; an even or zero window is refused; where $v_n \le 0$ the output is the mean.
2. `spectral_subtract`: the noise segment is the `noise` input (the same channel or the last one) or the first `noise_s` seconds of $x$ itself; it must be at least one window long. The noise frames include the edge frames half filled with padding zeros, as in scipy, so on a short segment $\hat P_k$ is slightly below the true noise power.
3. $\alpha > 1$ oversubtracts, $\beta$ leaves a noise floor instead of zero holes — less "musical noise".
4. `deconvolve`: three FFTs of length $N$; the kernel is no longer than the signal and starts at sample 0, the delay is not compensated. A linear (not circular) blur gives errors at the edges; a zero of $H_k$ is harmless — the denominator is at least $10^{-\mathrm{SNR}/10}$.

Cost: `wiener` $O(N K)$, spectral subtraction $O\big((N_x + N_z)/R\cdot N\log N\big)$, deconvolution $O(N\log N)$.

## With this block's numbers

Wiener: window {{block.size}}, noise variance {{block.noise}} (0 means estimated from the signal). Spectral subtraction: $\alpha$ = {{block.alpha}}, $\beta$ = {{block.beta}}, window {{block.n}} with a step of a quarter window; without the `noise` input the noise is taken from the first {{block.noise_s}} s. Deconvolution: SNR {{block.snr_db}} dB.

The node ran for {{block.wall}} and used {{block.bytes}}.

## Checks

- `wiener` matches `scipy.signal.wiener` to $10^{-12}$ for windows 5 and 11 with an estimated noise and 3 and 21 with a given noise of 0.04 and 0.01; windows 4 and 0 are refused; a constant signal gives a finite output.
- Spectral subtraction matches a reference built on `scipy.signal.stft` and `istft` to $10^{-12}$ for $(\alpha, \beta, N, R) = (2,\ 0.01,\ 64,\ 16)$, $(1,\ 0.05,\ 32,\ 8)$ and $(4,\ 0.001,\ 128,\ 64)$; noise or a signal shorter than a window is refused.
- Deconvolution matches the numpy FFT formula to $10^{-12}$ for SNR 20, 40 and 80 dB; at 120 dB it returns the clean signal to $10^{-4}$; an empty kernel and a kernel longer than the signal are refused; the kernel $(0.5,\ 0.5)$ with a zero at Nyquist gives a finite output.
- The blocks in a chain match the core to $10^{-12}$, including noise taken from the first 0.02 s of the signal; a window of 4096 on a short signal is refused.

## Links

- §12.1 — wavelet denoising, worth comparing these methods with.
- §11.9 — STFT and the COLA condition that the inverse transform relies on.
- §2.3 — circulants: circular convolution is diagonalised by the DFT, and deconvolution is a division of spectra.
- §2.6 — the Wiener filter among the project's Toeplitz problems.
- The "LMS and NLMS adaptive filter" and "Threshold — shrinking detail coefficients" cards.

## Implementation

Berouti subtraction: the power with a floor of $\beta\hat P_k$, the phase of the original frame.

```cpp
    for (auto& frame : z->frames)
        for (std::size_t k = 0; k < bins; ++k) {
            const double power = std::max(std::norm(frame[k]) - o.alpha * level[k], o.beta * level[k]);
            const double magnitude = std::abs(frame[k]);
            frame[k] = magnitude > 0 ? frame[k] * (std::sqrt(power) / magnitude) : C{std::sqrt(power), 0.0};
        }
```
