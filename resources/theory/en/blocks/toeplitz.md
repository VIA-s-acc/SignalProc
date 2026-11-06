+++
id = "toeplitz"
title = "Convolution as a Toeplitz matrix"
theory = ["toeplitz-convolution", "toeplitz-circulant", "toeplitz-definitions", "toeplitz-fast-matvec"]
source = "src/core/matrix.cpp"
function = "convolution_matrix"
test = "tests/core/test_matrix.cpp"
views = ["matrix"]
+++

## Purpose

Show the convolution $y = h * x$ as a matrix: a banded Toeplitz matrix for linear convolution and a circulant for circular convolution. It shows where the diagonal structure of filter banks comes from and why a circulant is fast to multiply.

## Formulas

Linear convolution is a lower-triangular banded matrix of width $L$:

$$y = T_h\, x, \qquad (T_h)_{ij} = h[i-j], \qquad T_h \in \mathbb R^{(N+L-1) \times N}, \qquad T_g T_h = T_{g * h}$$

Circular convolution is a circulant, diagonalized by the DFT:

$$C = \operatorname{circ}(c), \qquad C_{ij} = c[(i-j) \bmod N], \qquad C = F^{-1} \operatorname{diag}(\hat c)\, F, \qquad \lambda_k = \hat c[k]$$

Consequences: $Cx = F^{-1}(\hat c \cdot \hat x)$ with an element-wise product of the spectra, $\|C\|_2$ equals the largest $\lvert \hat c \rvert$, and the condition number equals the ratio of the largest $\lvert \hat c \rvert$ to the smallest.

## Algorithm and cost

1. Convolution matrix: column $j$ is a copy of $h$ shifted $j$ positions down. There are $NL$ non-zero entries; the band is $L$ diagonals.
2. Circulant: the taps of $h$ are accumulated at index $(\text{offset} + k) \bmod N$ into the vector $c$; column $j$ is its cyclic shift by $j$.
3. Dense matrices are built only for the explanation mode and for tests. Multiplication by $T_h$ is direct convolution in $O(NL)$; multiplication by $C$ is a real FFT, $\hat c \cdot \hat x$ and an inverse FFT in $O(N \log N)$ instead of the dense $O(N^2)$.

Square truncations of $T_h$ are not closed under multiplication: $T_g T_h = T_{g * h}$ holds exactly only for full rectangular matrices.

## With this block's numbers

Filter {{block.filter}}, a band of $L = {{block.band_width}}$ diagonals, a signal of length $N = {{block.n}}$. The convolution matrix has $N + L - 1$ rows and $N$ columns, the circulant is $N \times N$; the screen shows a {{block.rows}} × {{block.cols}} matrix.

The convolution matrix has $N \cdot L$ non-zero entries; the circulant has at most $N \cdot L$ (with $L > N$ the taps add up).

## Checks

- Multiplication by the circulant via the FFT matches the dense product $Cx$, largest deviation: {{check.circulant_fft}}
- The dense $T_h x$ equals the direct convolution; for full matrices $T_g T_h = T_{g * h}$ (test).
- The eigenvalues of the dense $C$ match the FFT of the first column $c$.

## Links

- §1.1 — linear and circular convolution and correlation.
- §2.1 — Toeplitz matrices and the symbol $a(\omega)$.
- §2.4 — Toeplitz plus Hankel: symmetric boundary extension.
- §2.5 — fast multiplication via embedding in a circulant.
- §6.4 — `periodization` mode: the block circulant of a filter bank.

## Implementation

Convolution matrix: column $j$ is obtained by shifting the coefficients of $h$ by $j$ rows down.

```cpp
Eigen::MatrixXd convolution_matrix(const Laurent& h, std::size_t n) {
    const std::size_t taps = h.coeffs.size();
    Eigen::MatrixXd m = Eigen::MatrixXd::Zero(idx(n + taps - 1), idx(n));
    for (std::size_t j = 0; j < n; ++j)
        for (std::size_t k = 0; k < taps; ++k) m(idx(j + k), idx(j)) = h.coeffs[k];
    return m;
}
```
