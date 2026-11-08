+++
id = "recursive"
title = "Recursive filters"
theory = ["lti-z-dtft", "cls-biquads", "cls-allpass-notch", "freq-zero-pole"]
source = "src/pipeline/blocks_effects.cpp"
function = "biquad_filter"
test = "tests/pipeline/test_blocks_more2.cpp"
views = ["under_the_hood"]
+++

## Purpose

Feedback (IIR) filters whose output depends on past outputs: `lfilter` runs any difference equation given $b$ and $a$, `biquad` is a second-order section from the RBJ recipes, `dc_block` removes the DC offset, `comb` is a comb with delay $D$.

## Formulas

`lfilter` is the difference equation with a zero initial state, as `scipy.signal.lfilter` without `zi`:

$$a_0\,y[n] = \sum_{k=0}^{M} b_k\,x[n-k] - \sum_{k=1}^{P} a_k\,y[n-k], \qquad H(z) = \frac{\sum_k b_k z^{-k}}{\sum_k a_k z^{-k}}$$

All roots of $A(z)$ must satisfy $\lvert p\rvert \le 1 + 10^{-6}$: a pole on the unit circle (the integrator $a = (1, -1)$) is allowed, $a_0 = 0$ is not.

`biquad` follows the RBJ cookbook, $\omega_0 = 2\pi f_0/f_s$, $\alpha = \sin\omega_0/(2Q)$, $A = 10^{G/40}$ ($G$ is `gain_db`). For example, the low-pass and the peaking filter:

$$b = \big(\tfrac{1-\cos\omega_0}{2},\ 1-\cos\omega_0,\ \tfrac{1-\cos\omega_0}{2}\big), \qquad a = (1+\alpha,\ -2\cos\omega_0,\ 1-\alpha)$$

$$b = (1+\alpha A,\ -2\cos\omega_0,\ 1-\alpha A), \qquad a = (1+\alpha/A,\ -2\cos\omega_0,\ 1-\alpha/A)$$

The `bandpass` type is $b = (\alpha, 0, -\alpha)$ with a 0 dB peak; the shelves use $2\sqrt{A}\,\alpha$ — the $Q$ form, not the slope $S$. The gain $G$ affects only `peaking`, `lowshelf` and `highshelf`.

`dc_block` has a zero at $z = 1$ and a pole at $r$; the corner is near $(1-r)\,f_s/2\pi$:

$$y[n] = x[n] - x[n-1] + r\,y[n-1], \qquad H(z) = \frac{1 - z^{-1}}{1 - r z^{-1}}$$

`comb` gives peaks or notches every $f_s/D$; the feedback kind requires $\lvert g\rvert < 1$:

$$y[n] = x[n] + g\,x[n-D] \quad (H = 1 + g z^{-D}), \qquad y[n] = x[n] + g\,y[n-D] \quad \Big(H = \frac{1}{1 - g z^{-D}}\Big)$$

## Algorithm and cost

1. `lfilter`: the coefficients are divided by $a_0$ and the roots of $A(z)$ are checked for stability; then the FIR part $b * x$ (cut to $N$, via the FFT for long $b$) and the recursion over $a$ in place — direct form I.
2. `biquad`: the RBJ coefficients are divided by $a_0$; $f_0$ must lie in $(0, f_s/2)$; the filter is transposed direct form II with two state variables.
3. `dc_block`: the same first-order scheme with one state variable.
4. `comb`: the delay $D$ is in samples or in seconds (`0.01s` gives $\mathrm{round}(0.01\,f_s)$), $D = 0$ is refused; for $n < D$ the output equals the input.

Cost of `lfilter` is $O\big(N\,(M+P)\big)$ plus root finding of degree $P \le 127$ (up to 128 coefficients in $a$); `biquad`, `dc_block` and `comb` are $O(N)$.

## With this block's numbers

`lfilter`: $b$ = [{{block.b}}], $a$ = [{{block.a}}].

`biquad`: type {{block.type}}, $f_0$ = {{block.f0}} Hz, $Q$ = {{block.q}}, gain {{block.gain_db}} dB.

`dc_block`: pole $r$ = {{block.r}}. `comb`: kind {{block.kind}}, delay {{block.delay}}, $g$ = {{block.gain}}.

Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- `lfilter` matches `scipy.signal.lfilter` to $10^{-10}$ on four $(b, a)$ pairs, including $a_0 = 2$ and the integrator $a = (1, -1)$; $a = (0, 1)$ and a pole at 1.5 give an error with the reason, the double pole $a = (1, -2, 1)$ is accepted.
- `biquad`: all eight types match `lfilter` with the RBJ coefficients to $10^{-10}$; $f_0 = 4000$ Hz at $f_s = 8000$ is refused.
- `comb`: the feedforward ($D = 9$, $g = -0.8$) and feedback ($D = 23$, $g = 0.6$) kinds match `lfilter` with a delay line to $10^{-10}$; feedback with $g = 1$ and $D = 0$ are refused.
- `dc_block`: a signal with a 0.5 offset at $r = 0.99$ matches `lfilter([1, -1], [1, -0.99])` to $10^{-12}$; on a constant input the output is below $10^{-12}$ by sample 4000.

## Links

- §1.2 — the z-transform and the DTFT.
- §9.11 — biquad sections and stability: a high order as one polynomial, as in `lfilter`, is sensitive to rounding.
- §9.8 — all-pass and notch filters.
- §8.6 — the zero–pole map.
- The "IIR design" card — how to get the coefficients from a specification.

## Implementation

Transposed direct form II: two state variables per section.

```cpp
    for (std::size_t n = 0; n < x.size(); ++n) {
        y[n] = f.b0 * x[n] + z0;
        z0 = f.b1 * x[n] - f.a1 * y[n] + z1;
        z1 = f.b2 * x[n] - f.a2 * y[n];
    }
```
