+++
id = "coeff_compression"
title = "Coefficient compression"
theory = ["audio-compression", "met-parseval"]
source = "src/pipeline/blocks_coeffs.cpp"
function = "keep_largest"
test = "tests/pipeline/test_blocks_more2.cpp"
views = ["decomposition"]
+++

## Purpose

Two compression steps in the coefficient domain of a decomposition: `keep_largest` keeps the $K$ coefficients of largest magnitude and zeroes the rest, `coeff_quantize` rounds the detail coefficients to a uniform grid with step $\Delta$. After the inverse transform you hear how much can be thrown away and coarsened.

## Formulas

Keeping the largest: the common pool $\mathcal P$ of a channel holds all bands $b \ge b_0$, where $b_0 = 1$ if the approximation (band 0) is left alone and $b_0 = 0$ otherwise. The number kept, with $[\cdot]$ rounding halves away from zero:

$$K = \min\Big(\lvert\mathcal P\rvert,\ \Big[\frac{a}{100}\,\lvert\mathcal P\rvert\Big]\Big) \quad \text{(percent)}, \qquad K = \min\big(\lvert\mathcal P\rvert,\ [a]\big) \quad \text{(count)}$$

The coefficients are sorted by $\lvert c\rvert$ in descending order, stably (on ties the earlier band and position wins); the first $K$ stay, the rest are zeroed. For an orthonormal bank with periodization the error equals the energy thrown away:

$$\sum_n \big(x[n] - \hat x[n]\big)^2 = \sum_{c\ \text{zeroed}} c^2$$

Quantization of bands $b \ge 1$ (the approximation is never quantized); with $B$ bits each band has its own step, with a fixed step all bands share it:

$$\Delta_b = \frac{\max_k\lvert c_b[k]\rvert}{2^{B-1} - 1}, \qquad \hat c = \mathrm{sign}(c)\,\Big\lfloor\frac{\lvert c\rvert}{\Delta_b} + \frac12\Big\rfloor\,\Delta_b, \qquad \lvert c - \hat c\rvert \le \frac{\Delta_b}{2}$$

This is a mid-tread quantizer: $2^B - 1$ levels $q\Delta_b$, $\lvert q\rvert \le 2^{B-1} - 1$; the band peak lands exactly on the outermost level, and $\lvert c\rvert < \Delta_b/2$ gives 0. With a fine step the quantization noise is $\approx \Delta_b^2/12$ per coefficient.

## Algorithm and cost

1. `keep_largest`: a pool of pointers to the channel's coefficients, a stable sort by magnitude, zeroing of the tail. Percentages above 100 and NaN in the input are refused.
2. The ranking is by $\lvert c\rvert$ with no weights: for biorthogonal banks and the SWT this is not the best $K$-term approximation (§12.2 suggests multiplying by the norms of the synthesis functions).
3. The decomposition screen sets the same keeping as a fraction and rounds $K$ up, while the block rounds to nearest.
4. `coeff_quantize`: in bits mode the step comes from the peak of each band; an all-zero band ($\Delta_b = 0$) is left unchanged. Unlike the dead-zone quantizer of §12.2 ($\lfloor\lvert c\rvert/\Delta\rfloor$ and reconstruction with an offset $\rho$), this is plain rounding: the zero step is $\Delta$ wide, not $2\Delta$.
5. For wavelet packets (`wpt`) band 0 is the first node of the basis.

Cost: keeping $O(P\log P)$ for $P$ coefficients, quantization $O(P)$.

## With this block's numbers

Mode {{block.mode}}: for `keep_largest` percent or count, for `coeff_quantize` bits or step. Keeping: {{block.amount}}, approximation left alone: {{block.keep_approx}}. Quantization: {{block.bits}} bits, that is $2^{B-1} - 1$ levels per sign, or step {{block.step}}.

The node ran for {{block.wall}} and used {{block.bytes}}.

## Checks

- `keep_largest` on a 3-level db2 decomposition matches a reference built on a stable `numpy.argsort` to $10^{-10}$ for "10 %, approximation kept", "25 coefficients, approximation included" and "37.5 %, approximation kept".
- $10^9$ coefficients leave everything as it was; 0 % keeps the approximation and zeroes the details; 150 % and NaN are refused with a clear message.
- `coeff_quantize` matches a numpy reference to $10^{-10}$ for 4 and 9 bits and for step 0.05; with 2 bits every detail coefficient is 0 or $\pm$ the band peak to $10^{-15}$.
- Keeping half of the coefficients of a wavelet-packet best basis gives a signal of the original length after `iwpt`.

## Links

- §12.2 — compression: the best $K$-term approximation and quantization.
- §14.3 — Parseval: when the error equals the sum of squares thrown away.
- The "Threshold — shrinking detail coefficients" and "Histogram and coefficient statistics" cards (band sparsity).

## Implementation

The pool of coefficients, the number $K$ and zeroing of everything outside the first $K$ by magnitude.

```cpp
        std::vector<double*> pool;
        for (std::size_t b = first; b < ch.bands.size(); ++b)
            for (double& v : ch.bands[b]) pool.push_back(&v);
        const auto total = static_cast<double>(pool.size());
        const auto keep = std::min(pool.size(), static_cast<std::size_t>(std::llround(percent ? amount / 100 * total : amount)));
        std::ranges::stable_sort(pool, std::greater<>{}, [](const double* v) { return std::abs(*v); });
        for (std::size_t k = keep; k < pool.size(); ++k) *pool[k] = 0.0;
```
