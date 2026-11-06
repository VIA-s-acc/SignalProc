+++
id = "equivalent_filters"
title = "Equivalent band filters"
theory = ["freq-equivalent-filters", "multirate-noble", "freq-band-metrics"]
source = "src/core/freq.cpp"
function = "equivalent_filter"
test = "tests/core/test_freq_bank.cpp"
views = ["filters", "decomposition"]
+++

## Purpose

Collapse the chain "filters of levels $1..j$ and downsamplings" into a single filter and a single downsampling by $2^j$, to see the magnitude response of each band of the pyramid on the original frequency scale: the octave partition, the overlap of neighboring bands and the side lobes.

## Formulas

By the noble identities (§3.3) the details and the approximation of level $j$ are obtained with one filter and $\downarrow 2^j$:

$$H^{\mathrm{eq}}_j(z) = H_1\big(z^{2^{j-1}}\big) \prod_{k=0}^{j-2} H_0\big(z^{2^k}\big), \qquad G^{\mathrm{eq}}_j(z) = \prod_{k=0}^{j-1} H_0\big(z^{2^k}\big)$$

Dilation $H(z^m)$ inserts $m-1$ zeros between the taps. With lengths $L_0 = L_1 = L$ the length of the equivalent filter is

$$L_j^{\mathrm{eq}} = (2^j - 1)(L - 1) + 1$$

Nominal bands: the detail $d_j$ — $[\pi/2^j,\ \pi/2^{j-1}]$, the approximation $a_j$ — $[0,\ \pi/2^j]$. The pywt downsampling phase (§0.4) is not taken into account: it shifts the samples of a band in time but does not change the magnitude response.

## Algorithm and cost

1. $g \leftarrow 1$. For $k = 0, \dots, j-2$: $g \leftarrow g * H_0(z^{2^k})$.
2. The last factor is $H_1$ for a detail or $H_0$ for the approximation, dilated by $2^{j-1}$; the result is its product with $g$.
3. The magnitude response of the equivalent filter is computed as for any FIR filter (a direct sum over a grid).

The products are dense: the cost is $O(4^{j} L^2)$ multiplications, and the length of the result grows as $2^j$. It is computed once per filter, not per signal.

## With this block's numbers

Bank "{{block.bank}}", band {{block.band}} — level $j = {{block.level}}$. The equivalent filter length is {{block.length}} samples (with equal lengths $L$ of the original filters $(2^j - 1)(L - 1) + 1$ is expected). Cutoff at $-3$ dB: $\omega_c = {{metric.cutoff_3db}}$.

The equivalent filters are taken from the analysis part of the bank; the PR of the bank itself (checks below) guarantees that the set of bands loses no information.

## Checks

- Bank distortion $T = z^{-\ell}$, largest deviation of the coefficients from a unit impulse: {{check.pr_distortion}}
- Bank aliasing $A \equiv 0$, largest coefficient magnitude: {{check.pr_alias}}
- For an orthogonal bank with $\sqrt2$ normalization the energy of the equivalent filter is $\|h^{\mathrm{eq}}_j\|^2 = 1$.
- For Haar, $a_3$ is a rectangle of eight samples $2^{-3/2}$, and $d_3$ is the difference of the halves (test). A direct multilevel DWT of a unit impulse gives the same impulse responses.

## Links

- §3.3 — the noble identities, which let downsampling move through a filter.
- §5.6 — Haar equivalent filters: a rectangle and a difference of rectangles.
- §8.7 — band metrics: cutoff, transition band, power symmetry.
- §11.1 — the Mallat pyramid, whose bands the equivalent filter describes.
- §11.3 — wavelet packets: nodes are products of $H_0 / H_1$ along the bits of the path, ordered by Gray code.

## Implementation

Dilating a filter with zeros and multiplying the dilated filters across levels.

```cpp
Laurent dilate(const Laurent& h, std::size_t m) {
    if (h.coeffs.empty() || m <= 1) return h;
    Laurent out{std::vector<double>((h.coeffs.size() - 1) * m + 1, 0.0), h.offset * static_cast<std::ptrdiff_t>(m)};
    for (std::size_t k = 0; k < h.coeffs.size(); ++k) out.coeffs[k * m] = h.coeffs[k];
    return out;
}

Result<Laurent> equivalent_filter(const FilterBank& bank, std::size_t level, bool detail) {
    if (level == 0) return make_error(Errc::invalid_argument, "equivalent_filter: level must be at least 1");
    Laurent g{{1.0}, 0};
    for (std::size_t k = 0; k + 1 < level; ++k) g = multiply(g, dilate(bank.h0, std::size_t{1} << k));
    const auto& last = detail ? bank.h1 : bank.h0;
    return multiply(dilate(last, std::size_t{1} << (level - 1)), g);
}
```
