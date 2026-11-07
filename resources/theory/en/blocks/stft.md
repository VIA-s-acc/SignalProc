+++
id = "stft"
title = "STFT spectrogram"
theory = ["stft", "freq-heisenberg"]
source = "src/core/spectral.cpp"
function = "stft_frames"
test = "tests/core/test_spectral.cpp"
views = ["spectrum"]
+++

## Purpose

Show how the frequency content of a signal changes over time, and feel the time–frequency trade-off: a long window separates close tones, a short one resolves short hits.

## Formulas

Frame $m$ with step $R$ and a Hann window $w$ of length $N$:

$$X(m,k)=\sum_{n=0}^{N-1} x[n+mR]\,w[n]\,e^{-2\pi i k n/N}, \qquad w[n]=\tfrac12-\tfrac12\cos(2\pi n/N)$$

Power on the "spectrum" scale, as in `scipy.signal.stft`: $P(m,k)=\lvert X(m,k)\rvert^2/\big(\sum_n w[n]\big)^2$.

Resolution: $\Delta t = N/f_s$, $\Delta f = f_s/N$; their product is at least $1/4\pi$ (§8.10).

## Algorithm and cost

1. The Hann window is periodic, as in scipy; the step is $R = N/4$ (75 % overlap).
2. Each frame is multiplied by the window; a real FFT gives $N/2+1$ bins.
3. Frames are folded into screen columns by mean power, bins into rows (log or linear axis).

Cost $O(M\,N\log N)$ for $M$ frames; memory is one frame plus the folded columns.

## With this block's numbers

Window $N$ = {{block.n}}, step {{block.hop}} (overlap {{block.overlap}}), $f_s$ = {{block.rate}}.

Resolution: $\Delta t$ = {{metric.dt}}, $\Delta f$ = {{metric.df}}; frame columns: {{metric.frames}}.

## Checks

- Frame power matches `scipy.signal.stft` to $10^{-12}$ of the maximum.
- The screen grid for $N = 512$ and $N = 4096$ matches the canvas data to 1 dB.

## Links

- §11.9 — STFT and the COLA condition.
- §8.10 — Heisenberg boxes.
- §9.3 — windows and leakage.

## Implementation

One frame: window, real FFT, power on the "spectrum" scale.

```cpp
        for (std::size_t k = 0; k < n; ++k) buffer[k] = x[f * hop + k] * window[k];
        pocketfft::r2c(shape, real_stride, complex_stride, 0, pocketfft::FORWARD, buffer.data(), spectrum.data(), 1.0);
        for (std::size_t b = 0; b < bins; ++b) power[b] = std::norm(spectrum[b]) * scale;
        out(f, power);
```
