+++
id = "hold_repeat"
title = "Sample and hold, repetition"
theory = ["multirate-down-up", "conv-signals"]
source = "src/pipeline/blocks_util.cpp"
function = "sample_hold"
test = "tests/pipeline/test_blocks_fx.cpp"
views = ["signal"]
+++

## Purpose

`sample_hold` keeps every $h$-th sample for $h$ ticks in a row — a staircase signal, like an ADC at a lower rate with no filter; `repeat` glues $T$ copies of the signal one after another.

## Formulas

Holding for $h$ samples, the output length equals the input length $N$:

$$y[k] = x\big[h\lfloor k/h \rfloor\big], \qquad 0 \le k < N$$

This is downsampling and zero insertion ($\downarrow h$, then $\uparrow h$) followed by convolution with a box $b_h[n] = 1$, $0 \le n < h$:

$$Y(e^{i\omega}) = B_h(e^{i\omega})\,\frac1h\sum_{j=0}^{h-1} X\big(e^{i(\omega - 2\pi j/h)}\big), \qquad \lvert B_h(e^{i\omega})\rvert = \frac{\lvert \sin(h\omega/2)\rvert}{\lvert \sin(\omega/2)\rvert}$$

The sample rate effectively drops to $f_s/h$: everything above $f_s/(2h)$ aliases, and the box droops the response and only partly suppresses the images.

Repeating $T$ times gives length $TN$: $y[n + tN] = x[n]$, $0 \le t < T$. The DFT of length $TN$:

$$Y[m] = \begin{cases} T\,X[m/T], & T \mid m \\ 0, & \text{otherwise} \end{cases}$$

The spectrum becomes a line spectrum: only every $T$-th bin is non-zero, and it is $T$ times higher. There is a jump at the seams if $x[N-1]$ is far from $x[0]$.

## Algorithm and cost

1. `sample_hold`: $h$ in samples or seconds is converted to samples, $h = 0$ is an error; one pass with the index $k - (k \bmod h)$.
2. `repeat`: a check that the length $TN$ is within the limit, then $T$ copies.

Cost $O(N)$ and $O(TN)$; memory — the output.

## With this block's numbers

Hold {{block.hold}} — the new sample rate is $f_s$ divided by the hold in samples.

Repetitions {{block.times}} — the length and the playing time grow by the same factor.

Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- `sample_hold` with $h = 5$ matches `x[np.arange(n) // 5 * 5]` exactly; a hold of 0.002 s at 1000 Hz maps (1, 2, 3, 4, 5) exactly to (1, 1, 3, 3, 5); $h = 0$ is rejected with the parameter's name.
- `repeat` with $T = 3$ on 50 samples equals `np.tile` exactly.

## Links

- §3.1 — downsampling and zero insertion: aliasing and images.
- §0.1 — the DFT: the line spectrum of a periodic extension.
- The "Downsampling by M" and "Upsampling by M" cards — the same operations on their own.

## Implementation

Holding: the sample index is rounded down to a multiple of $h$.

```cpp
                               return each_channel(a, [h = *hold](const Samples& c) {
                                   Samples y(c.size());
                                   for (std::size_t k = 0; k < c.size(); ++k) y[k] = c[k - k % h];
                                   return y;
                               });
```
