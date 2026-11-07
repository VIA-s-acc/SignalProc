+++
id = "iir_design"
title = "IIR design"
theory = ["cls-iir-prototypes", "cls-bilinear"]
source = "src/core/design/iir.cpp"
function = "iir_design"
test = "tests/core/test_iir.cpp"
views = ["filters"]
+++

## Purpose

Build a classical IIR filter (Butterworth, Chebyshev I and II, elliptic, Bessel) from its order and cutoffs, and split it into a cascade of biquads.

## Formulas

An analog prototype with a 1 rad/s cutoff is moved to the wanted frequency and band, then mapped by the pre-warped bilinear transform:

$$s = 2f_s\,\frac{z-1}{z+1}, \qquad \Omega = 2f_s\tan\frac{\omega}{2}$$

A zero or pole $a$ of the analog filter maps to $\dfrac{2f_s + a}{2f_s - a}$; missing zeros go to $z = -1$.

Butterworth: $\lvert H(j\Omega)\rvert^2 = \dfrac{1}{1 + \Omega^{2N}}$, exactly $-3$ dB at the cutoff.

## Algorithm and cost

1. The prototype in zero-pole-gain form, as in `scipy.signal`: Butterworth and Chebyshev by formula, elliptic through the complete integral $K(m)$ and the Jacobi functions, Bessel from the Bessel polynomial roots in 256-bit precision.
2. Low-pass → low-pass, high-pass, band-pass or band-stop (band filters double the order).
3. Bilinear transform and the stability check $\lvert p\rvert < 1$.
4. Biquad cascade: each pole pair with its nearest zeros.

Cost $O(N^2)$ for order $N$; the response is $O(N)$ per point.

## With this block's numbers

Filter: {{block.filter}}; order {{block.order}}, cutoff {{block.cutoff}}.

Poles: {{metric.poles}}; stability: {{check.stable}}.

## Checks

- Zeros, poles and gain match `scipy.signal.iirfilter` to $10^{-8}$; the cascade response to $10^{-9}$.
- Every pole of the designed filter lies inside the unit circle.

## Links

- §9.9 — analog IIR prototypes.
- §9.10 — the bilinear transform and pre-warping.
- §9.7 — high-pass, band-pass and band-stop from a low-pass prototype.

## Implementation

The bilinear transform of the zeros.

```cpp
    for (auto& z : f.z) {
        num *= fs2 - z;
        z = (fs2 + z) / (fs2 - z);
    }
```
