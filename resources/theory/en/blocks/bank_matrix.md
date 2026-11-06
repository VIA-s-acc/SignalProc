+++
id = "bank_matrix"
title = "Analysis and synthesis matrices"
theory = ["fb-matrices", "fbm-infinite", "fbm-block-toeplitz", "fbm-periodization", "fbm-boundary-modes"]
source = "src/core/matrix.cpp"
function = "analysis_matrix"
test = "tests/core/test_matrix.cpp"
views = ["matrix"]
+++

## Purpose

Write one level of the filter bank as a matrix: analysis $A$ as a stack of rows $[L;\ B]$ and synthesis $S$, which reveal perfect reconstruction, orthogonality and the conditioning of the transform, including its behavior at the boundaries.

## Formulas

Infinite matrices (Strang–Nguyen, [SN]): each row of $A$ is the reversed filter, neighboring rows are shifted by two; the columns of $S$ are shifted synthesis filters.

$$L_{k,n} = h_0[2k-n], \qquad B_{k,n} = h_1[2k-n], \qquad A = \begin{pmatrix} L \\ B \end{pmatrix}, \qquad (S_L)_{n,k} = f_0[n-2k], \qquad (S_B)_{n,k} = f_1[n-2k], \qquad S = \begin{pmatrix} S_L & S_B \end{pmatrix}$$

$$S A = S_L L + S_B B = I \quad \text{(PR)}, \qquad S = A^{\mathsf T} \iff f_i[n] = h_i[-n] \quad \text{(orthogonal bank)}, \qquad A^{\mathsf T} A = I$$

The implementation follows the pywt phase (§0.4): in the extension modes row $i$ is the convolution ending at sample $2i+1$, not $2i$. With periodization and even $N$ the matrix $A$ is a block circulant of $N/2$ blocks of size $2 \times 2$, and its spectrum is assembled from the symbol $H_p$:

$$\sigma(A) = \bigcup_{k=0}^{N/2-1} \sigma\Big(H_p\big(e^{2\pi i k / (N/2)}\big)\Big), \qquad \operatorname{cond}(A) = \frac{\sigma_{\max}}{\sigma_{\min}}$$

## Algorithm and cost

1. Band length per level $\mathrm{len}$ (here $L$ is the filter length): $\lfloor (N+L-1)/2 \rfloor$ for all modes except `periodization`, where it is $\lceil N/2 \rceil$. There are $2\,\mathrm{len}$ rows and $N$ columns.
2. Column $j$ is the result of one DWT level of the unit signal $e_j$ in the chosen edge mode: approximation coefficients on top, details below. This way the matrix accounts for the extension of the signal beyond the edges.
3. Synthesis $S$ is built the same way, by the inverse level on unit coefficients.
4. The interleaved row order takes rows in pairs $(a_k, d_k)$ — then the $2 \times 2$ block Toeplitz structure becomes visible (§6.2).
5. $\operatorname{cond}(A)$ — from the singular values (SVD).

Construction costs $N$ runs of a level at $O(NL)$ each, i.e. $O(N^2 L)$; SVD is $O(N^3)$. The matrices are built only for the explanation mode and for tests.

## With this block's numbers

Bank "{{block.bank}}", $N = {{block.n}}$, edge mode — {{block.mode}}, row order — {{block.order}}. Matrix size: {{block.rows}} × {{block.cols}} (rows × columns).

Condition number $\operatorname{cond}(A) = {{metric.cond}}$. For an orthogonal bank with periodization it equals $1$: all singular values are one. Outside periodization there are more rows than columns ($2\,\mathrm{len} > N$), and $A$ is not square.

## Checks

- Orthogonality $A^{\mathsf T} A = I$, largest deviation from the identity matrix: {{check.ata_identity}}. Holds for an orthogonal bank with periodization; at the edges in other modes and for biorthogonal banks this check fails by construction.
- Left invertibility $S A = I$ (perfect reconstruction), largest deviation of $S A x$ from $x$ on a test signal: {{check.sa_identity}}
- The spectrum via the symbol $H_p$ with periodization matches the dense SVD (§6.4).

## Links

- §6.1 — infinite analysis and synthesis matrices, biorthogonality $AS = I$.
- §6.2 — block Toeplitz structure and the symbol $H_p$.
- §6.3 — the multilevel transform as a product of matrices $W_J$.
- §6.4 — periodization: block circulant, exact $\operatorname{cond}(A)$ in $O(N)$.
- §6.5 — edge modes and output lengths.
- §13.4 — conditioning of biorthogonal pairs.

## Implementation

Analysis: each column of the matrix is one DWT level of a unit signal, approximation on top, details below.

```cpp
    const std::size_t len = dwt_coeff_len(n, bank.length(), options.mode);
    Eigen::MatrixXd a = Eigen::MatrixXd::Zero(idx(2 * len), idx(n));
    for (std::size_t j = 0; j < n; ++j) {
        const auto e = unit(n, j);
        const auto level = dwt_level<double>(e, bank, options);
        if (!level) return std::unexpected(level.error());
        for (std::size_t i = 0; i < len; ++i) {
            a(idx(i), idx(j)) = level->approx[i];
            a(idx(len + i), idx(j)) = level->detail[i];
        }
    }
    return a;
```
