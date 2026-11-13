# An entry of the Toeplitz matrix

Convolution of a signal of $N = 4$ samples with the filter `h = [{{h}}]` is multiplication by a $6 \times 4$ matrix $T(h)$. Find its entry $T_{ij}$ in row $i = {{i}}$ and column $j = {{j}}$; rows and columns are numbered from zero. The answer is a number.

## Hints {#hints}

1. Column $j$ of the matrix is a copy of $h$ shifted down by $j$ rows, the rest is zeros. Open the "Matrix" tab and look at it; see {#toeplitz-convolution}.
2. The matrix is Toeplitz: an entry depends only on the difference $i - j$. What is it when $i - j$ lies within $0 \dots L-1$, and what outside?
3. Column $j$ is the response to a unit impulse at position $j$, that is $T e_j = h * e_j$.

## Solution {#solution}

$T_{ij} = h[i - j]$ if $0 \le i - j \le L - 1$, and zero otherwise. For $i = {{i}}$, $j = {{j}}$ we get $T_{ij} = {{expected}}$. Along every diagonal of a Toeplitz matrix the number is the same, so all its $6 \cdot 4 = 24$ entries are set by the three samples of $h$; zeros appear where the filter has not yet slid into the signal or has already slid out. More in {#toeplitz-convolution}.
