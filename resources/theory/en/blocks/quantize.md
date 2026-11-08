+++
id = "quantize"
title = "Quantisation and mu-law"
theory = ["num-formats", "met-snr"]
source = "src/core/signal_ops.cpp"
function = "quantize"
test = "tests/core/test_ops.cpp"
views = ["signal"]
+++

## Purpose

`quantize` rounds the samples to the grid of a $b$-bit integer, optionally with triangular dither; `mulaw` compresses or expands the amplitude by the G.711 μ-law — a logarithmic curve that gives quiet sounds more quantisation levels.

## Formulas

Scale $Q = 2^{b-1}$, step $\Delta = 1/Q$. Dither $\delta = u_1 - u_2$ with $u_{1,2}$ uniform on $[0, 1)$ — a triangular distribution on $(-1, 1)$ in units of the step; without dither $\delta = 0$:

$$q[n] = \frac{1}{Q}\,\min\big(Q - 1, \max(-Q, \mathrm{round}(Q\,x[n] + \delta))\big)$$

Rounding is to the nearest, halves to even (as `numpy.round`); the range is $[-1, 1 - \Delta]$; NaN and infinities give 0.

Without dither the error is uniform on $[-\Delta/2, \Delta/2]$ with variance $\Delta^2/12$; for a full-scale sine

$$\mathrm{SNR} \approx 6.02\,b + 1.76\,\text{dB}$$

Dither adds $\Delta^2/6$, $\Delta^2/4$ in total: the SNR is $10\log_{10} 3 \approx 4.77$ dB lower, but the mean and the power of the error no longer depend on the signal, and the quantisation harmonics disappear.

μ-law ($\mu = 255$ in G.711), the input is clamped to $[-1, 1]$:

$$F(x) = \mathrm{sgn}(x)\,\frac{\ln(1 + \mu\lvert x\rvert)}{\ln(1 + \mu)}, \qquad F^{-1}(y) = \mathrm{sgn}(y)\,\frac{(1 + \mu)^{\lvert y\rvert} - 1}{\mu}$$

`mulaw` itself does not quantise: its output is continuous. The chain `mulaw` encode → `quantize` 8 bits → `mulaw` decode gives an input step

$$\Delta_x \approx \Delta_y\,\frac{\ln(1 + \mu)}{\mu}\,(1 + \mu\lvert x\rvert)$$

— for large $\mu\lvert x\rvert$ the relative error is almost constant, $\Delta_x/\lvert x\rvert \approx \Delta_y \ln(1 + \mu)$. This approximates G.711, where the curve is piecewise linear by segments.

## Algorithm and cost

1. `quantize`: an `mt19937_64` generator seeded with "seed + channel index" — channels get different dither, and a rerun with the same seed is exact.
2. Scale, dither, rounding, saturation, inverse scale — one sample at a time.
3. `mulaw`: NaN → 0, clamping to ±1, `log1p` and `expm1` for accuracy near zero.

Cost $O(N)$ per channel, memory — the output.

## With this block's numbers

Word length {{block.bits}} bits, dither {{block.dither}}, seed {{block.seed}}; the grid step is two to the power of one minus the word length.

μ-law: direction {{block.direction}}, $\mu$ = {{block.mu}}.

Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- Without dither `quantize` for 1, 2, 8, 16 and 24 bits matches `np.clip(np.round(y/Δ)·Δ, −1, 1−Δ)` exactly (error 0); 0 and 33 bits are rejected.
- With dither (8 bits) the result repeats with the same seed and changes with another; $\lvert q - x\rvert \le 1.5\Delta$, and every value lies on the grid.
- `mulaw_encode` matches the G.711 formula in numpy to $10^{-15}$; encode → decode returns the input on $[-1, 1]$ to $10^{-12}$; 2 and −3 give ±1, NaN gives 0, infinity gives 1.
- The blocks treat channels separately: 2 bits map (−1.5, −0.25, 0, 0.3, 1.2) exactly to (−1, 0, 0, 0.5, 0.5) (`tests/pipeline/test_blocks_basic.cpp`).

## Links

- §13.1 — formats and precision: double versus integer formats.
- §14.2 — SNR: how to measure the quantisation noise.
- The "Write WAV" card — the same quantisation when writing integer formats.

## Implementation

Quantising one sample: scale, triangular dither, rounding and saturation.

```cpp
    for (std::size_t n = 0; n < x.size(); ++n) {
        double v = std::isfinite(x[n]) ? x[n] * scale : 0.0;
        if (dither) {
            const double d1 = uniform(rng), d2 = uniform(rng);
            v += d1 - d2;
        }
        out[n] = std::clamp(std::nearbyint(v), -scale, scale - 1) / scale;
    }
```
