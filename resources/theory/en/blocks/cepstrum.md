+++
id = "cepstrum"
title = "Cepstrum and DCT"
theory = ["toeplitz-plus-hankel", "fb-spectral-factorisation", "lti-z-dtft"]
source = "src/core/analysis.cpp"
function = "real_cepstrum"
test = "tests/core/test_analysis.cpp"
views = ["under_the_hood"]
+++

## Purpose

Two "spectrum of a spectrum" transforms: `cepstrum` is the real cepstrum, where the period of a voice or an echo shows as a peak on the lag axis in samples (the quefrency), `dct` and `idct` are the orthonormal cosine transform and its inverse, on which MFCCs and compression rest.

## Formulas

The real cepstrum of the whole signal of length $N$ is the inverse DFT of the natural logarithm of the spectrum magnitude with a floor $\varepsilon$:

$$c[n] = \mathrm{Re}\,\frac{1}{N}\sum_{k=0}^{N-1} \ln\big(\lvert X[k]\rvert + \varepsilon\big)\,e^{2\pi i k n/N}, \qquad \varepsilon = 10^{-12}\max_k \lvert X[k]\rvert$$

The logarithm is of the magnitude, not of the power and not in dB; for silence ($\max\lvert X\rvert = 0$) the output is zeros. A periodicity $T_0$ gives a peak at $n = T_0 f_s$.

`dct` is the DCT-II with orthonormal scaling, as `scipy.fft.dct(x, type=2, norm="ortho")`:

$$X_k = \lambda_k\sqrt{\frac{2}{N}}\sum_{n=0}^{N-1} x[n]\cos\frac{\pi k(2n+1)}{2N}, \qquad \lambda_0 = \frac{1}{\sqrt2}, \quad \lambda_{k>0} = 1$$

`idct` is the transposed matrix, that is the DCT-III with the same scaling:

$$x[n] = \sqrt{\frac{2}{N}}\sum_{k=0}^{N-1} \lambda_k X_k \cos\frac{\pi k(2n+1)}{2N}$$

The matrix is orthogonal: $\sum_n x[n]^2 = \sum_k X_k^2$ and `idct` of `dct` returns the signal.

## Algorithm and cost

1. `cepstrum`: a complex FFT of length $N$, the maximum magnitude for the floor, the logarithm, the inverse FFT, the real part. There are no frames — the cepstrum is taken over the whole channel at once.
2. `dct` and `idct`: the fast DCT-II and DCT-III from pocketfft with the factor $1/\sqrt{2N}$ and orthonormal scaling of the zeroth coefficient.
3. The output length and sample rate equal the input ones.

Cost $O(N\log N)$ per channel; memory $O(N)$.

## With this block's numbers

No parameters: the output length equals the input length $N$. Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- `real_cepstrum` matches the `numpy` reference `real(ifft(log(abs(fft(x)) + 1e-12 * max)))` to $10^{-9}$ for lengths 37, 64 and 1.
- `dct2` matches `scipy.fft.dct(type=2, norm="ortho")` to $10^{-12}$, `idct2` of `dct2` returns the signal to $10^{-12}$; the DCT-II matrix of size 9 is orthogonal and matches the fast transform to $10^{-14}$.
- In the pipeline `idct` of `dct` restores the second stereo channel to $10^{-12}$, the `cepstrum` block equals the core per channel, the cepstrum of silence is exactly zero.

## Links

- §2.4 — Toeplitz plus Hankel: the DCT-II diagonalises the symmetric extension.
- §4.7 — spectral factorisation: Kolmogorov's cepstral method also goes through the $\log$ of the spectrum and an inverse FFT.
- §1.2 — the z-transform and the DTFT.
- The `spectral_features` card: MFCCs are the DCT of the log mel energies.

## Implementation

The logarithm of the spectrum magnitude with a floor from the maximum, then the inverse FFT.

```cpp
    auto spectrum = fft(std::vector<C>(x.begin(), x.end()), true);
    double peak = 0;
    for (const auto& v : spectrum) peak = std::max(peak, std::abs(v));
    if (!(peak > 0)) return std::vector<double>(x.size(), 0.0);
    for (auto& v : spectrum) v = std::log(std::abs(v) + 1e-12 * peak);
    const auto back = fft(std::move(spectrum), false);
```
