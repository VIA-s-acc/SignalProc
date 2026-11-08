+++
id = "lpc"
title = "Linear prediction"
theory = ["toeplitz-levinson", "lti-z-dtft"]
source = "src/pipeline/blocks_spectral.cpp"
function = "lpc_of"
test = "tests/pipeline/test_blocks_more2.cpp"
views = ["under_the_hood"]
+++

## Purpose

`lpc` describes a signal by an all-pole model of order $p$: it finds the predictor and reflection coefficients, the prediction gain and the residual — what could not be predicted. `lpc_synth` passes the residual through the inverse filter and returns the signal exactly.

## Formulas

The autocorrelation with no window and no division by the length:

$$r[\ell] = \sum_{n=\ell}^{N-1} x[n]\,x[n-\ell], \qquad \ell = 0, \dots, p$$

Levinson–Durbin for $A(z) = 1 + \sum_{j=1}^{p} a_j z^{-j}$, $E_0 = r[0]$:

$$k_m = -\frac{r[m] + \sum_{j=1}^{m-1} a^{(m-1)}_j\,r[m-j]}{E_{m-1}}, \qquad a^{(m)}_j = a^{(m-1)}_j + k_m\,a^{(m-1)}_{m-j}, \qquad a^{(m)}_m = k_m$$

$$E_m = (1 - k_m^2)\,E_{m-1}, \qquad G = 10\log_{10}\frac{r[0]}{E_p}\ \mathrm{dB}$$

Signs: the prediction is $\hat x[n] = -\sum_j a_j x[n-j]$, and $k_m$ equals the last coefficient $a^{(m)}_m$; the partial autocorrelation at lag $m$ is $-k_m$.

The residual is the FIR filter $A(z)$, the synthesis the IIR filter $1/A(z)$, both with a zero initial state:

$$e[n] = x[n] + \sum_{j=1}^{\min(p,n)} a_j\,x[n-j], \qquad y[n] = e[n] - \sum_{j=1}^{\min(p,n)} a_j\,y[n-j]$$

## Algorithm and cost

1. The autocorrelation per channel; with $r[0] = 0$ (silence) all $a_j = k_j = 0$, $G = 0$ and the residual equals the signal.
2. The recursion goes order by order; if $\lvert k_m\rvert \ge 1$ or $E_m \le 0$ the block stops: the order is too high for the signal (a pure tone, for example).
3. The report: `order`, `gain_db`, `a1…ap`, `k1…kp`, with a `_chN` suffix for several channels.
4. `lpc_synth` reads the order and the $a_j$ from the report; the report and the residual must have the same number of channels.

Cost $O(N p)$ for the autocorrelation and for each filter, plus $O(p^2)$ for the recursion.

## With this block's numbers

Model order $p$ = {{block.order}}: that many coefficients $a_j$ and $k_j$ per channel in the report. The node ran for {{block.wall}} and used {{block.bytes}}.

## Checks

- For $p = 4$ and $p = 12$ the coefficients $a_j$ match `scipy.linalg.solve_toeplitz` to $10^{-10}$, the reflections $k_j$ a reference Levinson–Durbin recursion to $10^{-10}$, `gain_db` to $10^{-10}$, and the residual `scipy.signal.lfilter([1, a], 1, x)` to $10^{-10}$.
- `lpc_synth` returns the original signal to $10^{-10}$, including three channels with a short silent channel that has $a_1 = 0$ and $G = 0$.
- An empty report and a report with another channel count are refused; order 64 on a pure 64-sample cosine either succeeds or is refused with a message about the order; a single sample 0.5 comes back unchanged.

## Links

- §2.6 — Levinson–Durbin and the condition $\lvert k_m\rvert < 1$ for a positive definite Toeplitz matrix.
- §1.2 — the z-transform: $A(z)$ and the all-pole envelope $1/\lvert A(e^{i\omega})\rvert^2$.
- The "LMS and NLMS adaptive filter" card — normal equations of the same kind, solved as the signal goes.

## Implementation

One step of the Levinson–Durbin recursion: the reflection, the update of the coefficients and the error, the stability check.

```cpp
    for (std::size_t m = 1; m <= order; ++m) {
        double acc = r[m];
        for (std::size_t j = 1; j < m; ++j) acc += a[j] * r[m - j];
        const double k = -acc / error;
        previous = a;
        for (std::size_t j = 1; j < m; ++j) a[j] = previous[j] + k * previous[m - j];
        a[m] = k;
        error *= 1 - k * k;
        if (!(std::abs(k) < 1) || !(error > 0)) return fail(tr("pipeline.error.lpc_order"), fmt::arg("order", order));
        out.k[m - 1] = k;
    }
```
