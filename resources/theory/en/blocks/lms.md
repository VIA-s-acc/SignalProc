+++
id = "lms"
title = "LMS and NLMS adaptive filter"
theory = ["lti-convolution", "toeplitz-levinson"]
source = "src/core/adaptive.cpp"
function = "adaptive_filter"
test = "tests/core/test_adaptive.cpp"
views = ["under_the_hood"]
+++

## Purpose

The `lms` block tunes an FIR filter sample by sample so that its response to a reference $x$ reproduces a desired signal $d$: this cancels interference correlated with the reference (output $e$ is the cleaned signal) or identifies an unknown system (output $y$ is its model).

## Formulas

The vector of the last $L$ reference samples (zeros before the start), the output and the a priori error with the old weights:

$$u_n = \big(x[n],\ x[n-1],\ \dots,\ x[n-L+1]\big)^{T}, \qquad y[n] = w_n^{T}u_n, \qquad e[n] = d[n] - y[n]$$

LMS is a stochastic gradient step (without the factor 2 that some textbooks write):

$$w_{n+1} = w_n + \mu\,e[n]\,u_n$$

NLMS is the same step divided by the energy of the reference window, $\varepsilon = 10^{-8}$:

$$w_{n+1} = w_n + \frac{\mu}{\varepsilon + u_n^{T}u_n}\,e[n]\,u_n$$

Both approach the Wiener–Hopf solution $R\,w_{\mathrm{opt}} = p$ with the Toeplitz $R = E[u_n u_n^{T}]$ and $p = E[d[n]\,u_n]$. NLMS converges for $0 < \mu < 2$ whatever the level of $x$; LMS for $0 < \mu < 2/\lambda_{\max}(R)$, and $\mu < 2/(L\,\sigma_x^2)$ is sufficient.

## Algorithm and cost

1. Each channel of $d$ takes the channel of $x$ with the same number, or the last one; the sampling rates and lengths of $x$ and $d$ must agree, $1 \le L \le N$.
2. The weights start at zero; at every sample the window $u$ shifts, then $y$, the window energy and $e$ are computed and the weights are updated.
3. If the error becomes infinite or NaN, the block stops and suggests a smaller $\mu$.
4. Outputs: $e$ is the signal without what the reference predicts; $y$ is the estimate of that part. The final weights are not output.

Cost $O(N L)$ per channel, memory $O(L)$.

## With this block's numbers

Rule {{block.rule}}, filter length $L$ = {{block.taps}}, step $\mu$ = {{block.mu}}. For NLMS stability is guaranteed for $\mu < 2$; for LMS compare $\mu$ with $2/(L\,\sigma_x^2)$ from the power of the reference.

The node ran for {{block.wall}} and used {{block.bytes}}.

## Checks

- LMS ($\mu = 0.02$) and NLMS ($\mu = 0.5$) match a numpy reference ($y$, $e$ and the final weights to $10^{-10}$) in two scenarios: identifying a 5-tap system with an 8-tap filter and cancelling filtered interference with a 6-tap filter.
- NLMS identifies the system: the weights differ from its taps by less than 0.01, and the extra taps are within 0.01 of zero.
- Mismatched lengths, $L = 0$, $L > N$ and $\mu = 0$ are refused; a diverging LMS ($\mu = 1.5$, 16 taps) stops with a message about `mu`; with a zero reference NLMS gives $e = d$ exactly — $\varepsilon$ guards against division by zero.
- The `lms` block in a chain gives the same $e$ and $y$ to $10^{-10}$, keeps the rate and refuses a reference with a different rate.

## Links

- §1.1 — convolution: $y[n]$ is an FIR filter with the current weights.
- §2.6 — Toeplitz systems: the Wiener–Hopf equations that LMS solves iteratively and Levinson–Durbin directly.
- The "Linear prediction" card (Toeplitz normal equations of the same kind, solved at once) and the "Signal restoration" card (a Wiener filter without adaptation).

## Implementation

The error, the divergence check and the weight update by the LMS or NLMS rule.

```cpp
        const double e = d[n] - y;
        if (!std::isfinite(e))
            return make_error(Errc::no_convergence, "adaptive filter: diverged at sample " + std::to_string(n) + "; lower mu (LMS is stable below 2/(taps·power of x))");
        r.y[n] = y;
        r.e[n] = e;
        const double step = rule == AdaptiveRule::nlms ? mu * e / (eps + power) : mu * e;
        for (std::size_t k = 0; k < taps; ++k) r.w[k] += step * u[k];
```
