+++
id = "halfband"
title = "Halfband filter and splits"
theory = ["fb-maxflat", "fb-p0-factorisations", "fb-spectral-factorisation"]
source = "src/core/design/split.cpp"
function = "bank_from_split"
test = "tests/core/test_split.cpp"
views = ["designer"]
+++

## Purpose

Build the maximally flat halfband filter $P_p$ and split its roots between $H_0$ and $F_0$: each split gives a two-channel bank with perfect reconstruction, and the Sobolev exponent tells whether it generates a wavelet basis.

## Formulas

$$P_p(z) = 2\left(\frac{(1+z)(1+z^{-1})}{4}\right)^{p} B_p\left(\frac{2-z-z^{-1}}{4}\right), \qquad B_p(y) = \sum_{k=0}^{p-1} \binom{p-1+k}{k}\, y^k$$

$$P_p(z) + P_p(-z) = 2, \qquad P_0(z) = z^{-(2p-1)}\, P_p(z), \qquad \deg P_0 = 4p-2$$

The roots $y_j$ of the polynomial $B_p$ give pairs $z_j, 1/z_j$ from the equation $z^2 - c_j z + 1 = 0$, $c_j = 2 - 4 y_j$. A split: $k$ zeros at $z = -1$ and a subset $S$ of root groups go to $H_0$, the rest goes to $F_0$:

$$H_0(z) \propto (1+z^{-1})^{k} \prod_{g \in S} (1 - z_g z^{-1}), \qquad F_0(z) \propto (1+z^{-1})^{2p-k} \prod_{g \notin S} (1 - z_g z^{-1}), \qquad H_0(1) = F_0(1) = \sqrt2$$

The Sobolev exponent of a side with $N$ zeros at $-1$ (Villemoes), $m(\omega) = \left(\tfrac{1+e^{-i\omega}}{2}\right)^N L(\omega)$:

$$s = N - \log_4 \rho(T_W), \qquad (T_W)_{n,k} = 2\, w_{2n-k}, \qquad \lvert L(e^{i\omega}) \rvert^2 = \sum_j w_j\, e^{ij\omega}$$

A Riesz wavelet basis is obtained when $s > 0$ for both sides.

## Algorithm and cost

1. The coefficients of $B_p$ are integer binomials; the roots $y_j$ of degree $p-1$ are found by Aberth iteration in 256-bit arithmetic. The zeros at $z = -1$ are not passed to the root finder.
2. Each $y_j$ gives a pair $z_j, 1/z_j$. A real root is a separate group; a complex root forms a group together with its conjugate. There are $g$ groups, with $p-1 \le g \le 2(p-1)$.
3. For each $k = 0, \dots, 2p$ and each of the $2^g$ subsets of groups, $H_0$ and $F_0$ are assembled and normalized to $\sqrt2$, the halfband property of $P_0$ is checked, and the filters are fitted into the pywt layout (§0.4).
4. The split is classified as orthogonal, linear-phase or other; $s$ is computed for both sides.

In total there are $(2p+1)\,2^{g}$ splits, each requiring a product of polynomials of length $O(p)$. For $p = 2$: $5 \cdot 4 = 20$ splits, 6 of them wavelet ones.

## With this block's numbers

$p = {{block.p}}$: the filter $P_0$ has degree $4p-2 = {{block.degree}}$ and $2p$ zeros at $z = -1$. The enumeration gives {{block.splits}} splits, of which {{block.wavelet_splits}} are wavelet ones.

Selected split "{{block.split}}": $H_0$ gets $k = {{block.zeros_h0}}$ zeros at $-1$, the length of $H_0$ is {{block.h0_length}}; $F_0$ gets $2p-k = {{block.zeros_f0}}$ zeros, the length of $F_0$ is {{block.f0_length}}. The bank delay in the pywt layout is $\ell = {{block.delay}}$.

Sobolev exponents: $s(\tilde\varphi) = {{metric.sobolev_h0}}$ for $H_0$ and $s(\varphi) = {{metric.sobolev_f0}}$ for $F_0$.

## Checks

- Aliasing $A \equiv 0$, $\max$ of the coefficient magnitudes of $\tfrac12\left[F_0(z) H_0(-z) + F_1(z) H_1(-z)\right]$: {{check.pr_alias}}
- Distortion $T = z^{-\ell}$, largest deviation of the coefficients of $\tfrac12\left[F_0 H_0 + F_1 H_1\right]$ from a unit impulse: {{check.pr_distortion}}
- Orthogonality $\sum_n h_0[n]\, h_0[n+2k] = \delta[k]$ and $f_0$ is the reversed $h_0$: {{check.orthogonality}}. For biorthogonal splits this check fails by construction.
- The halfband property $P_p(z) + P_p(-z) = 2$ holds identically for any $p$ — it is the Bézout identity, not a numerical result.

## Links

- §4.2 — PR as the halfband property of $P_0$, where the delay $\ell$ comes from.
- §4.4 — the orthogonal case: $F_0$ is the reversal of $H_0$, $P$ is non-negative on the circle.
- §4.7 — rules for distributing the roots: dbN, symlets, biorthogonal banks.
- §4.8 — all 20 factorizations for $p = 2$ and the verdict on each.
- §7.6 — regularity: what the Sobolev exponent means for $\varphi$.
- §13.5 — root finding: why the multiple zero at $-1$ is not searched for numerically.

## Implementation

A split: the powers of $(1+z^{-1})$ and the factors of the root groups are distributed between $H_0$ and $F_0$ (`one_plus` is the polynomial $1 + z^{-1}$), then the bank is normalized and checked for PR.

```cpp
auto h0 = power(one_plus, split.zeros_h0);
auto f0 = power(one_plus, 2 * f.p - split.zeros_h0);
for (std::size_t g = 0; g < f.groups.size(); ++g) {
    if (split.group_in_h0[g])
        h0 = multiply(h0, group_factor(f.groups[g]));
    else
        f0 = multiply(f0, group_factor(f.groups[g]));
}
return make_bank<double>(std::move(name), normalised(h0), normalised(f0));
```
