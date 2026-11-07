+++
id = "fir_design"
title = "FIR design"
theory = ["cls-parks-mcclellan", "cls-least-squares"]
source = "src/core/design/fir.cpp"
function = "remez"
test = "tests/core/test_fir.cpp"
views = ["filters"]
+++

## Purpose

Build a linear-phase FIR filter from its bands: equiripple (Parks–McClellan, the Remez algorithm) or least squares.

## Formulas

Remez minimises the largest weighted error over the bands $F$:

$$\min_h \max_{\omega \in F} W(\omega)\,\lvert D(\omega) - A(\omega)\rvert$$

Least squares minimises the integral of the squared error:

$$\min_h \int_F W(\omega)\,\big(D(\omega) - A(\omega)\big)^2\,d\omega$$

## Algorithm and cost

1. Remez: a dense grid (16 points per coefficient) and an exchange of extremal frequencies until the set stops changing; the taps come from an inverse DFT of the found amplitude response.
2. Least squares: the system $Q a = b$ with Toeplitz and Hankel parts is solved by a Cholesky factorisation.

Remez costs $O(K\,L\,G)$ for $K$ iterations, $L$ taps and grid size $G$; least squares $O(L^3)$.

## With this block's numbers

Filter: {{block.filter}}; length {{block.length}}, cutoff {{block.cutoff}}.

Ripple {{metric.ripple_db}}, attenuation {{metric.attenuation_db}}.

## Checks

- Remez matches `scipy.signal.remez` to $10^{-8}$ (band-pass, Hilbert, differentiator, even and odd lengths); least squares matches `scipy.signal.firls` to $10^{-10}$.
- The impulse response is symmetric or antisymmetric: the phase is linear.

## Links

- §9.6 — Parks–McClellan.
- §9.5 — least squares.
- §9.2 — linear-phase FIR types.

## Implementation

Exchange iterations: the deviation must grow, otherwise the design does not converge.

```cpp
    for (int iteration = 0; iteration < spec.max_iterations; ++iteration) {
        const double dev = std::abs(rz.calc_parms());
```
