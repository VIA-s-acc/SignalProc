+++
id = "threshold"
title = "Threshold — shrinking detail coefficients"
theory = ["audio-denoising"]
source = "src/core/process/shrink.cpp"
function = "denoise"
test = "tests/core/test_shrink.cpp"
views = ["decomposition"]
+++

## Purpose

Treat small detail coefficients $d_j$ as noise and zero or shrink them, keeping the large ones that carry the signal: the model $y = x + \sigma e$ with white Gaussian $e$.

## Formulas

Noise estimate from the finest level (the median of $\lvert N(0,1)\rvert$ is $0.6745$) and the thresholds:

$$\hat\sigma = \frac{\operatorname{median}\,\lvert d_1[k]\rvert}{0.6745}, \qquad \lambda_{\mathrm{Visu}} = \hat\sigma\sqrt{2\ln N}, \qquad \lambda_{\mathrm{Bayes}} = \frac{\hat\sigma^2}{\hat\sigma_X}$$

$$\hat\sigma_X = \sqrt{\max\left(\hat\sigma_Y^2 - \hat\sigma^2,\, 0\right)}, \qquad \hat\sigma_Y^2 = \frac{1}{N_j}\sum_k d_j[k]^2$$

Shrinkage functions of a coefficient $y$ with threshold $\lambda$ (hard, soft, non-negative garrote):

$$\eta_H(y) = y\,\mathbf 1\{\lvert y\rvert > \lambda\}, \qquad \eta_S(y) = \operatorname{sign}(y)\,(\lvert y\rvert - \lambda)_+, \qquad \eta_G(y) = \left(y - \frac{\lambda^2}{y}\right)\mathbf 1\{\lvert y\rvert > \lambda\}$$

Result: $\hat d_j[k] = \eta\big(d_j[k];\, \lambda_j\big)$, the approximation $a_J$ is unchanged, $\hat x = \mathrm{IDWT}\big(a_J, \hat d_J, \dots, \hat d_1\big)$.

## Algorithm and cost

1. DWT to $J$ levels (in `denoise` the default is $J_{\max} - 3$, but not less than 1).
2. $\hat\sigma$ — from $d_1$ (zero coefficients are excluded from the median) or set manually.
3. For each detail $d_j$ a threshold $\lambda_j$: VisuShrink — one for all levels, BayesShrink — from the energy of its own band; as $\hat\sigma_X \to 0$ the threshold becomes huge and the band is suppressed entirely.
4. The shrinkage function is applied to each $d_j$; with $\hat\sigma = 0$ (silence) the signal is returned as is.
5. IDWT.

The soft function is continuous but shifts large coefficients by $\lambda$; the hard one shifts less but leaves "musical noise"; the garrote is a compromise.

Cost: analysis and synthesis at $\approx N\,(L_0 + L_1)$ multiplications each, the median and shrinkage $O(N)$, memory $O(N)$.

## With this block's numbers

Rule {{block.rule}}, shrinkage function {{block.mode}}, affected levels: {{block.levels}}.

Noise estimate $\hat\sigma = {{metric.sigma}}$; per-level thresholds $\lambda_j$: {{block.thresholds}}.

Detail coefficients kept: {{metric.kept}}; detail energy removed: {{metric.energy_removed}}.

## Checks

It would be worth checking the identity at $\lambda = 0$, the estimate $\hat\sigma$ on the Donoho–Johnstone test signals (within 5%) and agreement with pywt and scikit-image — these checks live in `tests/core/test_threshold.cpp` and `test_shrink.cpp`; they are not on screen.

## Links

- §12.1 — the noise model, SureShrink and averaging over cyclic shifts.
- §11.1 — the Mallat pyramid, where the $d_j$ come from.
- §11.2 — SWT: thresholding not tied to the shift phase.
- §12.3 — zeroing a whole band, a crude threshold and the aliasing it produces.
- §14.2 — SNR: how to measure the gain from denoising.
- [M] — estimation and thresholding in wavelet bases.

## Implementation

Per-level thresholds from the estimate $\hat\sigma$, then each detail is shrunk with the chosen function.

```cpp
DenoiseReport report{options.sigma.value_or(noise_sigma_mad(coeffs->bands.back())), levels, {}};
for (std::size_t b = 1; b < coeffs->bands.size(); ++b) {
    const double t = options.rule == ShrinkRule::bayes ? bayes_threshold(coeffs->bands[b], report.sigma)
                                                       : universal_threshold(report.sigma, x.size());
    report.thresholds.push_back(t);
}
if (report.sigma == 0) return std::pair{std::vector<double>(x.begin(), x.end()), std::move(report)};

for (std::size_t b = 1; b < coeffs->bands.size(); ++b)
    coeffs->bands[b] = threshold(coeffs->bands[b], report.thresholds[b - 1], options.threshold);
```
