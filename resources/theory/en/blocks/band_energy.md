+++
id = "band_energy"
title = "Energy per band"
theory = ["met-parseval"]
source = "src/core/metrics.cpp"
function = "band_energy_fractions"
test = "tests/core/test_signal_ops.cpp"
views = ["decomposition"]
+++

## Purpose

Show how the signal energy is distributed over the decomposition bands, and confirm that an orthonormal transform preserves it.

## Formulas

Energy of band $b$ and its share, where $c_b \in \{d_1, \dots, d_J, a_J\}$:

$$E_b = \sum_k c_b[k]^2, \qquad p_b = \frac{E_b}{\sum_{b'} E_{b'}}$$

Orthonormal bank with periodization — Parseval's identity:

$$\sum_n x[n]^2 = \sum_{j=1}^{J}\sum_k d_j[k]^2 + \sum_k a_J[k]^2$$

Biorthogonal bank, Riesz bounds $A$ and $B$ (§14.3): $A\,\|x\|^2 \le \sum_b E_b \le B\,\|x\|^2$.

## Algorithm and cost

1. For each band $E_b$ is a sum of squares with Neumaier compensation: the error is of order $u$, not $Nu$ (§13.2).
2. The total energy $T = \sum_b E_b$ — also compensated.
3. Share $p_b = E_b / T$; when $T = 0$ all shares are zero.

Cost $O(N)$: each coefficient is read once, and there are $\approx N$ of them in total; memory $O(J)$.

To compare bands of different length $N_b$, the energy is divided by the length: $E_b / N_b$ is the power per sample (§12.5).

## With this block's numbers

Bands: {{block.bands}}.

Band energies $E_b$ or their shares $p_b$: {{metric.energies}}.

## Checks

- Parseval, $\lvert \sum_b E_b - \|x\|^2 \rvert / \|x\|^2$: {{check.parseval}}
- The equality holds for an orthonormal bank with periodization; in other edge modes and for biorthogonal banks there are only Riesz bounds.

## Links

- §14.3 — Parseval and Riesz bounds.
- §6.4 — periodization: the block circulant that makes the bank orthogonal.
- §13.2 — compensated summation for energies.
- §12.5 — per-band energy features.
- §14.4 — coding gain from band variances.
- §11.1 — where the bands $d_j$ and $a_J$ come from.

## Implementation

Energy of each band and normalization by the sum. For SWT a level-$j$ band gets weight $2^{-j}$: without decimation it carries $2^j$ times the MODWT energy.

```cpp
std::vector<double> band_energy_fractions(const BasicCoeffs<double>& coeffs, std::span<const double> weights) {
    std::vector<double> e;
    e.reserve(coeffs.bands.size());
    for (std::size_t k = 0; k < coeffs.bands.size(); ++k) e.push_back(energy(coeffs.bands[k]) * (k < weights.size() ? weights[k] : 1.0));
    const double total = neumaier_sum(e);
    for (double& v : e) v = total > 0 ? v / total : 0.0;
    return e;
}
```
