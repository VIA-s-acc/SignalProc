+++
id = "arithmetic"
title = "Arithmetic on signals"
theory = ["conv-signals", "freq-magnitude"]
source = "src/pipeline/blocks_util.cpp"
function = "math_defined"
test = "tests/pipeline/test_blocks_fx.cpp"
views = ["signal"]
+++

## Purpose

Sample-by-sample operations on one or two signals: `affine` is $sx + o$, `math` a function of one signal, `binary` a function of two signals, `mix` a sum with gains or a crossfade from one signal to the other, `abs` and `square` the magnitude and the square.

## Formulas

One input:

$$y_{\mathrm{affine}} = s\,x + o, \qquad y_{\mathrm{abs}} = \lvert x\rvert, \qquad y_{\mathrm{square}} = x^2$$

`math`: $\ln x$, $\log_{10} x$, $e^x$, $\sqrt{x}$, $x^p$, $\mathrm{sgn}\,x \in \{-1, 0, 1\}$, $1/x$. Domain: $x > 0$ for the logarithms, $x \ge 0$ for the root, $x \ne 0$ for $1/x$; for $x^p$, $x < 0$ with a non-integer $p$ and $x = 0$ with $p < 0$ are forbidden. The first sample outside the domain stops the block, reporting the sample index, the channel and the value.

`binary`: $a/b$ ($b \ne 0$), $\min(a, b)$, $\max(a, b)$, $a^b$ (the same limits as $x^p$), $\sqrt{a^2 + b^2}$ (`hypot`, with no overflow in the intermediate square).

`mix` with gains $g_a = 10^{G_a/20}$, $g_b = 10^{G_b/20}$; the `sum` mode and the `crossfade` mode:

$$y[n] = g_a a[n] + g_b b[n], \qquad y[n] = (1 - w[n])\,g_a a[n] + w[n]\,g_b b[n]$$

$$w[n] = \min\big(1, \max(0, (n - n_0)/L)\big), \qquad n_0 = \lfloor t_0 f_s + 1/2 \rfloor, \quad L = \lfloor \tau f_s/1000 + 1/2 \rfloor$$

With $L = 0$ it is a hard switch at sample $n_0$. The crossfade is linear in amplitude: the weights sum to 1, so for uncorrelated signals the power in the middle of the fade is 3 dB lower.

`abs` and `square` are nonlinear and create new frequencies: $\cos^2\omega_0 n = \tfrac12 + \tfrac12\cos 2\omega_0 n$ — squaring doubles the frequency and adds a DC term; the magnitude of a sine gives DC and even harmonics.

Inputs of different lengths: the shorter one is padded with zeros to the length of the longer (for `binary`, dividing by those zeros is a domain error); channels are paired by index, and the input with fewer channels repeats its last one; the sample rates must match.

## Algorithm and cost

1. One pass over the samples; `math` and `binary` check the domain before computing.
2. If a result becomes $\pm\infty$ or NaN (for instance $e^{800}$ or $(10^{200})^2$), the block does not return it but reports an error.
3. `mix` computes the weight $w[n]$ on the fly; the `sum` mode has no weight.

Cost $O(N)$ per channel, memory — the output.

## With this block's numbers

`affine`: scale {{block.scale}}, offset {{block.offset}}. `math` and `binary`: operation {{block.op}}, exponent {{block.p}}.

`mix`: gains {{block.gain_a_db}} and {{block.gain_b_db}} dB, mode {{block.mode}}, the crossfade starts at {{block.at}} s and lasts {{block.crossfade_ms}} ms.

Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- `affine` with scale −1.5 and offset 0.25 matches numpy to $10^{-15}$; every `math` operation with $p = 2.5$ matches numpy to $10^{-12}$.
- Values outside the domain (the logarithm of zero, the root of −1, $1/0$, $(-2)^{0.5}$, $0^{-1}$) are rejected, the message names the operation, the sample and the value; $(-2)^3 = -8$ exactly; $e^{800}$ is an error.
- `binary`: divide, min, max and hypot with a shorter second signal (300 samples) and pow match numpy to $10^{-12}$; dividing by the padded zeros and mismatched sample rates are errors; a one-channel second input applies to both channels.
- `mix` (−3 and +2 dB, a crossfade from 4 ms lasting 5 ms) matches numpy to $10^{-12}$; with length 0 the switch is exact: (1, 1, 2, 2) (`tests/pipeline/test_blocks_more2.cpp`).
- `abs` and `square` give exact values on (−2, 0.5, 3); the square of $10^{200}$ is rejected (`tests/pipeline/test_blocks_more.cpp`).

## Links

- §0.1 — a signal as a sequence of samples: every operation here is sample-wise.
- §8.2 — decibels and $20\lg$ for the `mix` gains.
- The "Gain in dB" and "Memoryless nonlinearities" cards.

## Implementation

The `math` domain check before computing: a refusal instead of NaN or infinity.

```cpp
bool math_defined(std::string_view op, double x, double p) {
    if (op == "log" || op == "log10") return x > 0;
    if (op == "sqrt") return x >= 0;
    if (op == "reciprocal") return x != 0;
    if (op == "pow") return !(x < 0 && !whole(p)) && !(x == 0 && p < 0);
    return true;
}
```
