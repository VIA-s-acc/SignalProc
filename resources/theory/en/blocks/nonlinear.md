+++
id = "nonlinear"
title = "Memoryless nonlinearities"
theory = ["conv-signals", "multirate-down-up"]
source = "src/pipeline/blocks_fx.cpp"
function = "wavefold"
test = "tests/pipeline/test_blocks_fx.cpp"
views = ["signal"]
+++

## Purpose

Pass every sample through the same memoryless function $y = F(x)$: `clip` is a hard cut at a threshold, `softclip` a smooth $\tanh$ saturation, `wavefold` folds the wave back into $[-1, 1]$. This is how overdrive and new harmonics are made.

## Formulas

Hard clipping at threshold $A$ and soft saturation with drive $k$:

$$y_{\mathrm{clip}} = \begin{cases} x, & \lvert x\rvert \le A \\ A\,\mathrm{sgn}\,x, & \lvert x\rvert > A \end{cases} \qquad y_{\mathrm{soft}} = \tanh(kx)$$

Folding: $u = kx$, $v = (u + 1) \bmod 4 \in [0, 4)$ — a triangle wave of period 4 in $u$, which is just $y = u$ for $\lvert u\rvert \le 1$; the `sine` shape is the smooth variant:

$$y_{\mathrm{tri}} = \begin{cases} v - 1, & v < 2 \\ 3 - v, & v \ge 2 \end{cases} \qquad y_{\sin} = \sin\frac{\pi u}{2}$$

All the functions are odd, $F(-x) = -F(x)$: a sine $f_0$ at the input gives only odd harmonics $3f_0, 5f_0, \dots$. For small $kx$, $\tanh(kx) \approx kx - (kx)^3/3$, and $\sin^3\theta = \tfrac34\sin\theta - \tfrac14\sin 3\theta$ — the third harmonic of a tone of amplitude $a$ is about $(ka)^3/12$.

There is no oversampling: harmonics above $f_s/2$ are not suppressed and reflect to $\lvert n f_0 - j f_s\rvert$ — the same aliasing as downsampling without a filter.

## Algorithm and cost

1. Every sample of every channel is processed on its own: a comparison, $\tanh$, `fmod` or $\sin$.
2. `clip` in the core (`hard_clip`) also counts the clipped samples; the block returns only the signal.
3. Length, channel count and sample rate do not change.

Cost $O(N)$ per channel, memory — the output.

## With this block's numbers

`clip`: threshold {{block.limit}} — everything larger in magnitude becomes exactly ± the threshold.

`softclip` and `wavefold`: drive {{block.drive}}, folding shape {{block.shape}}; with drive 1 the triangle shape leaves a signal inside $[-1, 1]$ unchanged.

Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- `wavefold` with drive 3 matches the numpy formulas for both shapes to $10^{-12}$ and stays within $[-1, 1]$; with drive 1 the triangle shape keeps 0.25 and −0.5 exactly.
- `softclip` with drive 2 matches `numpy.tanh(2x)` to $10^{-15}$ (`tests/pipeline/test_blocks_more.cpp`).
- `clip` at 0.5 maps (−1.5, −0.25, 0, 0.3, 1.2) exactly to (−0.5, −0.25, 0, 0.3, 0.5) (`tests/pipeline/test_blocks_basic.cpp`).

## Links

- §0.1 — the signal and the DFT: new harmonics are new non-zero bins of the spectrum.
- §3.1 — aliasing: frequencies above Nyquist reflect into the band.
- The `thd` block (spectral features card) measures the share of harmonics; the "Dynamics" card — a level-dependent gain, but with memory.

## Implementation

Folding: a triangle wave of period 4 via `fmod`, the sine shape in one function.

```cpp
                               return each_sample(in, [drive, sine](double x) {
                                   const double u = drive * x;
                                   if (sine) return std::sin(kPi / 2 * u);
                                   double v = std::fmod(u + 1, 4.0);
                                   if (v < 0) v += 4;
                                   return v < 2 ? v - 1 : 3 - v;
                               });
```
