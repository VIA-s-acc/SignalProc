+++
id = "band_edit"
title = "Band gains and zeroing"
theory = ["audio-subband-zeroing"]
source = "src/tui/screens/decomposition_model.cpp"
function = "reconstruct"
test = "tests/tui/test_decomposition_model.cpp"
views = ["decomposition"]
+++

## Purpose

Multiply the selected bands by gains (zero means zeroing) and assemble the signal back; show the price of such editing — lost energy and aliasing.

## Formulas

$$\hat x = \mathrm{IDWT}\big(g_a\, a_J,\; g_J\, d_J,\; \dots,\; g_1\, d_1\big), \qquad g = 10^{G/20}, \qquad g = 0 \text{ for a disabled band}$$

For an orthonormal bank with periodization (§14.3) the energy adds up over the bands:

$$\|\hat x\|^2 = g_a^2\,\|a_J\|^2 + \sum_{j=1}^{J} g_j^2\,\|d_j\|^2$$

The price of editing. If $g \ne 1$, the $X(-z)$ term is no longer cancelled. For two channels with the details zeroed

$$\hat X(z) = \tfrac12 F_0(z)\big[H_0(z)X(z) + H_0(-z)X(-z)\big]$$

and the aliasing magnitude $\tfrac12\,\lvert F_0(e^{i\omega})\,H_0(e^{i(\omega+\pi)})\rvert$ peaks in the transition band near $\pi/2$: for Haar it is $\tfrac12\lvert\sin\omega\rvert$, i.e. $-6$ dB.

For $J$ levels $\hat X(z) = \sum_k A^{(g)}_k(z)\,X(zW^k)$, $A^{(g)}_k = \sum_i \frac{g_i}{M_i}\,F^{eq}_i\,H^{eq}_i(zW^k)$; with $g \equiv 1$ only $A_0 = z^{-\ell}$ remains.

## Algorithm and cost

1. The DWT is computed once and stored; changing a gain or enabling a band repeats only steps 2–3.
2. Each band is multiplied by its own $g_b$; a disabled band is filled with zeros.
3. IDWT of the modified coefficients; the energy of $\hat x$ is compared with the energy of $x$.

Cost: $N$ multiplications by gains plus synthesis $\approx N\,(L_0 + L_1)$; memory $O(N)$.

How to reduce aliasing: a smooth transition of gains across neighboring bands instead of a hard zero; long filters (db20, sym20); SWT/MODWT-MRA, where zeroing a detail is linear time-invariant filtering.

## With this block's numbers

Bands enabled: {{block.enabled}}; gains $G$ in dB: {{block.gains}}.

Share of the energy of $x$ remaining in $\hat x$: {{metric.energy_kept}}.

Aliasing created by the editing: {{block.aliasing}}.

## Checks

It would be worth checking that unit gains return $x$ up to rounding, that enabling a band returns the original signal, and that the aliasing energy (§14.6) is small for the chosen strategy — these checks live in `tests/tui/test_decomposition_model.cpp` and `tests/core/test_shrink.cpp`; they are not on screen.

## Links

- §12.3 — why zeroing subbands produces aliasing and how to reduce it.
- §8.11 — response of the analysis → synthesis chain with gains, $A^{(g)}_k$.
- §14.6 — aliasing energy and lack of shift invariance.
- §4.2 — how synthesis cancels aliasing when $g \equiv 1$.
- §11.2 — SWT/MODWT: zeroing without aliasing.
- §12.1 — thresholding as "soft" zeroing of small coefficients.

## Implementation

Gains multiply the bands, disabled bands are zeroed, then synthesis assembles $\hat x$.

```cpp
for (std::size_t i = 0; i < processing.gains_db.size() && i <= J; ++i)
    if (processing.gains_db[i] != 0) {
        const double g = std::pow(10.0, processing.gains_db[i] / 20);
        for (double& c : bands[J - i]) c *= g;
    }
auto kept = rec->processed;
for (std::size_t i = 0; i <= J && i < enabled.size(); ++i)
    if (!enabled[i]) std::fill(kept.bands[J - i].begin(), kept.bands[J - i].end(), 0.0);
auto xhat = inverse_transform(kept, bank, d->options);
```
