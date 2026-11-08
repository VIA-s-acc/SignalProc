+++
id = "statistics"
title = "Histogram and coefficient statistics"
theory = ["met-other", "audio-band-energy", "wavelet-packets"]
source = "src/pipeline/blocks_analysis.cpp"
function = "histogram_channel"
test = "tests/pipeline/test_blocks_analysis.cpp"
views = ["under_the_hood"]
+++

## Purpose

Describe how values are distributed rather than in what order they come: `histogram` counts the samples of each channel into equal bins, like `numpy.histogram`; `coeff_stats` reports for every band of a decomposition its energy share, entropy and sparsity — how well the band lends itself to compression.

## Formulas

A histogram with $B$ bins on $[lo, hi]$ (by default $lo = \min x$, $hi = \max x$; when $lo = hi$, $[lo - \tfrac12, hi + \tfrac12]$):

$$e_i = lo + i\,\frac{hi - lo}{B}, \quad e_B = hi, \qquad i(v) = \min\Big(B - 1,\ \Big\lfloor B\,\frac{v - lo}{hi - lo}\Big\rfloor\Big)$$

Bins are half-open $[e_i, e_{i+1})$, the last one closed $[e_{B-1}, hi]$; the index $i(v)$ is corrected by one against the actual edges $e_i$, as numpy does. Values outside $[lo, hi]$ go to `outside`; the mode is the middle of the fullest bin $(e_k + e_{k+1})/2$, the first one on ties.

Statistics of band $b$ with coefficients $c_b[k]$, $k < N_b$:

$$\varepsilon_b = \frac{w_b E_b}{\sum_j w_j E_j}, \qquad E_b = \sum_k c_b[k]^2, \qquad H_b = -\sum_k p_k\log_2 p_k, \quad p_k = \frac{c_b[k]^2}{E_b}$$

$$s_b = \frac{\#\big\{k : \lvert c_b[k]\rvert < \tau\,c_{\max}\big\}}{N_b}, \qquad c_{\max} = \max_{j,k}\lvert c_j[k]\rvert$$

The weights are $w_b = 1$, except for an unnormalised SWT: $w(a_J) = 2^{-J}$, $w(d_j) = 2^{-j}$ — this undoes the energy growth of the undecimated levels, and the shares agree with the "Energy per band" block. The entropy is in bits, from 0 (all energy in one coefficient) to $\log_2 N_b$ (energy spread evenly). The sparsity threshold is relative to the largest magnitude over all bands of the channel, not within the band.

## Algorithm and cost

1. `histogram`: the edges and the minimum and maximum are per channel; `range` is two numbers $lo < hi$ or `auto`; NaN and infinite samples are refused.
2. If $hi - lo$ overflows (for example $\pm 1.7\cdot 10^{308}$), everything is scaled by $\tfrac12$ before the division, so the index stays finite.
3. `coeff_stats`: per band, one sum of squares, one sum of $p\log_2 p$ and one count of small coefficients. With $E_b = 0$ the entropy is 0; with $c_{\max} = 0$ the sparsity is 1.
4. Fields are named `c<channel>.<band>.energy|entropy|sparsity`; wavelet-packet bands are named by their paths in the tree.

Cost $O(N)$ per channel for the histogram and $O\big(\sum_b N_b\big)$ for the statistics; memory is $B$ counters.

## With this block's numbers

Histogram: {{block.bins}} bins on the range {{block.range}}. Coefficient statistics: a coefficient counts as small when its magnitude is below {{block.tol}} of the channel's largest.

The node ran for {{block.wall}} and used {{block.bytes}}.

## Checks

- `histogram` matches `numpy.histogram`: counts, $lo$, $hi$ and the number of values outside the range exactly, the mode to $10^{-15}$; the cases are 10 bins on the auto range, 7 bins on $[-1, 1]$ and 4 bins on a constant signal.
- Samples $0$, $0.5$, $1$ in two bins give "1 2" — the right edge belongs to the last bin; the range "1 0" and NaN are refused with a clear message; a second channel is counted separately.
- A huge range $\pm 1.7\cdot 10^{308}$ and a degenerate constant signal lose no samples: all of them land in bins.
- `coeff_stats` on db2, 3 levels, $\tau = 0.05$: the shares, entropies and sparsities of all bands match a numpy and pywt reference to $10^{-12}$; the SWT shares match the `band_energy` block to $10^{-15}$; a zero signal gives sparsity 1 and entropy and share 0; packet bands are named by their paths.

## Links

- §14.7 — other metrics.
- §12.5 — energy shares per band.
- §11.3 — the entropy cost of the best basis; there the coefficients are normalised by $\sum_n x[n]^2$, here by the energy of the band.
- The "Energy per band" and "Coefficient compression" cards.

## Implementation

Sorting samples into bins, with the index corrected against the actual edges.

```cpp
    for (const double v : x) {
        if (!(v >= lo && v <= hi)) continue;
        const double position = (v * scale - lo * scale) / span * static_cast<double>(bins);
        if (!std::isfinite(position)) continue;
        auto k = static_cast<std::size_t>(std::clamp(position, 0.0, static_cast<double>(bins - 1)));
        if (k > 0 && v < edges[k]) --k;
        if (k + 1 < bins && v >= edges[k + 1]) ++k;
        ++counts[k];
        ++inside;
    }
```
