+++
id = "filter_bank"
title = "Two-channel filter bank"
theory = ["two-channel", "fb-distortion-alias", "fb-alias-cancellation", "fb-cqf", "conv-pywt"]
source = "src/core/bank.cpp"
function = "check_pr"
test = "tests/core/test_bank.cpp"
views = ["designer", "filters"]
+++

## Purpose

Describe the bank $(h_0, h_1, f_0, f_1)$: analysis, downsampling $\downarrow 2$, zero insertion $\uparrow 2$ and synthesis — and check that the output is the original signal with a pure delay $\ell$, i.e. without aliasing or distortion.

## Formulas

Analysis channel $v_i = {\downarrow}2\,(h_i * x)$, synthesis $\hat x = \sum_i f_i * {\uparrow}2\, v_i$. Then

$$\hat X(z) = T(z)\, X(z) + A(z)\, X(-z), \qquad T = \tfrac12\left[F_0 H_0 + F_1 H_1\right], \qquad A = \tfrac12\left[F_0(z) H_0(-z) + F_1(z) H_1(-z)\right]$$

PR with delay $\ell$: $A \equiv 0$ and $T = z^{-\ell}$. The standard choice of highpass filters (Strang–Nguyen, [SN]) cancels aliasing for any $H_0, F_0$:

$$H_1(z) = F_0(-z), \qquad F_1(z) = -H_0(-z) \quad \Rightarrow \quad A \equiv 0, \qquad T = \tfrac12\left[P_0(z) - P_0(-z)\right], \qquad P_0 = F_0 H_0$$

Hence PR $\iff P_0(z) - P_0(-z) = 2 z^{-\ell}$: $P_0$ has a single odd coefficient $p_0[\ell] = 1$. In zero-based arrays this is $h_1[n] = (-1)^n f_0[n]$ and $f_1[n] = (-1)^{n+1} h_0[n]$ (§0.4).

Orthogonal bank (CQF, length $L$ even):

$$f_0[n] = h_0[L-1-n], \qquad \sum_n h_0[n]\, h_0[n+2k] = \delta[k]$$

## Algorithm and cost

1. The Laurent polynomials $T$ and $A$ are computed by exact products of the coefficients $F_0 H_0$, $F_1 H_1$ and their counterparts with $H_i(-z)$.
2. Aliasing — the largest-magnitude coefficient of $A$.
3. Distortion: the position of the largest $|T|$ gives $\ell$; the measure is the largest deviation of the coefficients of $T$ from a unit impulse at position $\ell$.
4. PR is accepted if both measures are within the tolerance; then the delay $\ell$ is defined as well.

Cost — four filter products at $O(L^2)$ each, independent of the signal length. Running the bank itself on a signal costs $\approx N\,(L_0 + L_1)$ multiplications per level.

## With this block's numbers

Bank "{{block.bank}}": filter lengths — {{block.length}}, linear phase — {{block.linear_phase}}. PR delay $\ell = {{block.delay}}$ samples: the output is $\hat x[n] = x[n - \ell]$.

In the pywt layout all four filters have the same even length $L$, and $P_0 = F_0 H_0$ is centered at $L - 1$; hence $\ell = L - 1$ (Haar: $\ell = 1$; 5/3 with $L = 6$: $\ell = 5$).

## Checks

- Aliasing $A \equiv 0$, largest coefficient magnitude of $A$: {{check.pr_alias}}
- Distortion $T = z^{-\ell}$, largest deviation of the coefficients of $T$ from an impulse at position $\ell$: {{check.pr_distortion}}
- Orthogonality $\sum_n h_0[n]\, h_0[n+2k] = \delta[k]$ and $f_0$ is the reversed $h_0$: {{check.orthogonality}}. For biorthogonal banks (5/3, 9/7) this check fails by construction: linear phase and orthogonality are incompatible, except for Haar.

## Links

- §4.1 — the distortion and aliasing equations.
- §4.2 — how to choose synthesis so that aliasing cancels itself.
- §4.4 — CQF: the orthogonal bank as a special case.
- §4.5 — polyphase form of PR and paraunitarity.
- §5.2 — a worked example on the Haar bank: analysis, $\downarrow 2$, $\uparrow 2$, synthesis step by step.
- §0.4 — highpass signs and correspondence with PyWavelets.

## Implementation

PR check: the position of the peak of $T$ gives the delay $\ell$; the distortion measure is the deviation of the other coefficients from zero and of the peak from one; aliasing and $T$ have been assembled beforehand as filter products.

```cpp
    if (distortion.coeffs.empty()) {
        report.distortion.value = 1.0;
        return report;
    }
    const auto peak = std::ranges::max_element(distortion.coeffs, {}, [](double c) { return std::abs(c); });
    const auto ell = distortion.offset + (peak - distortion.coeffs.begin());
    double deviation = 0.0;
    for (std::size_t k = 0; k < distortion.coeffs.size(); ++k) {
        const auto n = distortion.offset + static_cast<std::ptrdiff_t>(k);
        deviation = nan_max(deviation, std::abs(distortion.coeffs[k] - (n == ell ? 1.0 : 0.0)));
    }
    report.distortion.value = deviation;
    if (report.distortion.passed() && report.alias.passed()) report.delay = ell;
```
