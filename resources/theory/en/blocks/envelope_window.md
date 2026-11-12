+++
id = "envelope_window"
title = "Windows and fades"
theory = ["cls-window-method", "stft"]
source = "src/pipeline/blocks_level.cpp"
function = "tukey"
test = "tests/pipeline/test_blocks_more.cpp"
views = ["signal"]
+++

## Purpose

`window` multiplies every whole channel by a window of its length (Hann, Hamming, Blackman, Kaiser or Tukey); `fade` raises the level smoothly from zero at the start and lowers it to zero at the end.

## Formulas

The windows are symmetric, as `scipy.signal.get_window(..., fftbins=False)`: the denominator is $N - 1$, $\theta_n = 2\pi n/(N-1)$:

$$w_{\mathrm{hann}} = \tfrac12 - \tfrac12\cos\theta_n, \qquad w_{\mathrm{hamming}} = 0.54 - 0.46\cos\theta_n, \qquad w_{\mathrm{blackman}} = 0.42 - 0.5\cos\theta_n + 0.08\cos 2\theta_n$$

$$w_{\mathrm{kaiser}}[n] = \frac{I_0\big(\beta\sqrt{1 - r_n^2}\big)}{I_0(\beta)}, \qquad r_n = \frac{2n}{N-1} - 1$$

Tukey with fraction $\alpha$: cosine tapers over $m = \lfloor \alpha(N-1)/2 \rfloor$ samples on each side, the middle equals 1:

$$w[n] = \tfrac12\big(1 + \cos\pi(\tfrac{2n}{\alpha(N-1)} - 1)\big), \quad n \le m; \qquad w[n] = \tfrac12\big(1 + \cos\pi(\tfrac{2n}{\alpha(N-1)} - \tfrac{2}{\alpha} + 1)\big), \quad n \ge N - m - 1$$

$\alpha \le 0$ is the rectangular window, $\alpha \ge 1$ is Hann; for $N < 2$ the window is 1.

Multiplication in time is a circular convolution of the spectra: $Y[k] = \frac1N\sum_j X[j]\,W[(k - j) \bmod N]$. The spectrum of the signal is smeared by the main lobe of the window, and the side lobes set the leakage.

A fade of length $L$ at the start and $L'$ at the end with shape $\varphi$, for $i < L$ and $i < L'$ respectively:

$$y[i] = \varphi(i/L)\,x[i], \qquad y[N-1-i] = \varphi(i/L')\,x[N-1-i], \qquad \varphi(t) = t \quad \text{or} \quad \varphi(t) = \tfrac12 - \tfrac12\cos\pi t$$

The first and the last samples become zero, the last changed sample gets $\varphi((L-1)/L) < 1$ rather than 1; if $L + L' > N$, the factors multiply. The cosine shape is $\sin^2(\pi t/2)$: the rise and the fall sum to 1.

## Algorithm and cost

1. The window is built afresh for every channel from its length (`symmetric_window` in the core; Tukey in the block itself).
2. Element-wise multiplication.
3. `fade`: lengths in samples or seconds are converted to samples; two passes — over the start and over the end.

Cost $O(N)$ per channel; Kaiser takes two Bessel functions $I_0$ per sample. Memory — the window and the output.

## With this block's numbers

Window {{block.kind}}; Kaiser $\beta$ {{block.beta}}, Tukey taper fraction {{block.alpha}}.

Fade: rise {{block.fade_in}}, fall {{block.fade_out}}, shape {{block.curve}}.

Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- Every window (Hann, Hamming, Blackman, Kaiser with $\beta = 8.6$ and $3$, Tukey with $\alpha = 0.5$ and $0.2$) on 37 ones matches `scipy.signal.get_window(..., fftbins=False)` to $10^{-13}$; a single sample is left unchanged.
- `fade` on six ones with rise 4 and fall 2 gives exactly (0, 0.25, 0.5, 0.75, 0.5, 0); a cosine rise of length 2 gives 0 and 0.5 to $10^{-15}$.

## Links

- §9.3 — the window method: main-lobe width, side-lobe level and the Kaiser formula.
- §11.9 — STFT: there the Hann window is periodic (denominator $N$), here it is symmetric.
- The "STFT spectrogram" card — the same window, but on every frame.

## Implementation

The Tukey tapers — the same expression as in scipy.

```cpp
    const double last = static_cast<double>(n - 1);
    const auto width = static_cast<std::size_t>(std::floor(alpha * last / 2.0));
    for (std::size_t k = 0; k <= width; ++k)
        w[k] = 0.5 * (1 + std::cos(std::numbers::pi * (-1 + 2.0 * static_cast<double>(k) / alpha / last)));
    for (std::size_t k = n - width - 1; k < n; ++k)
        w[k] = 0.5 * (1 + std::cos(std::numbers::pi * (-2.0 / alpha + 1 + 2.0 * static_cast<double>(k) / alpha / last)));
```
