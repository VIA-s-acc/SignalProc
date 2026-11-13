# How many coefficients per level

A signal of $N = {{n}}$ samples is decomposed by one level with the wavelet `db{{p}}`. How many coefficients does each of the two bands $a$ and $d$ get with periodization (`periodization`) and with symmetric extension (`symmetric`)? The answer is two whole numbers separated by a space: first for periodization, then for symmetric extension.

## Hints {#hints}

1. Periodization is circular convolution: the signal is not lengthened, and after decimation half of the samples remain, rounded up; see {#fbm-periodization}.
2. Symmetric extension is an ordinary convolution with an extended signal: the band length depends on the filter length $L$, not only on $N$. The length of `db{{p}}` is $2 \cdot {{p}}$; look for the formula in the table {#fbm-boundary-modes}.
3. Check on the "Matrix" tab: with periodization the matrix is square, with extension it is rectangular, with more rows than columns.

## Solution {#solution}

The answer is {{expected}}. The first number is periodization, $\lceil N/2 \rceil$; the second is symmetric extension, $\lfloor (N + L - 1)/2 \rfloor$ with $L = 2 \cdot {{p}}$. Both lengths refer to one band; the total is twice as many coefficients. Circular convolution stores exactly $N$ coefficients (critical sampling), while with extension a convolution with a filter of length $L$ gives $N + L - 1$ samples, and after ↓2 there are $\lfloor (N + L - 1)/2 \rfloor$ per band, which is more than half of $N$. Details and the table of modes are in {#fbm-boundary-modes}.
