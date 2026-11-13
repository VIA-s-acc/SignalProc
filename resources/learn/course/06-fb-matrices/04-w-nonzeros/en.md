# Nonzero entries of a multilevel W

The multilevel matrix $W_J$ of the Haar bank for a signal of $N = {{n}}$ samples and $J = {{j}}$ levels (periodization) is square, $N \times N$; its rows are the basis functions $a_J, d_J, \dots, d_1$. How many nonzero entries does it have? The answer is a whole number.

## Hints {#hints}

1. Column $j$ of the matrix holds the coefficients of all bands for a unit impulse at position $j$. How many nonzero coefficients does each band have if the wavelet is Haar? See {#fbm-multilevel}.
2. Count by rows: a row of band $d_k$ has support of length $2^k$, a row of $a_J$ has length $2^J$; how many rows does each band have?
3. Check on the "Matrix" tab: operation "multilevel", the Haar wavelet, your $N$ and $J$.

## Solution {#solution}

There are {{expected}} nonzero entries. Each of the $J$ detail levels gives $N$ of them: $N/2^k$ rows of $2^k$ entries. Band $a_J$ gives the same $N$: $N/2^J$ rows of $2^J$ entries. In total $N(J + 1)$; a dense matrix would hold $N^2$ entries, so the nonzero share is $(J + 1)/N$. Counting by columns gives the same: every impulse has exactly one nonzero coefficient in each of the $J + 1$ bands. More in {#fbm-multilevel}.
