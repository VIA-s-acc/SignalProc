# An entry of the Haar analysis matrix

The analysis matrix $A$ of the Haar bank for $N = 8$ with periodization is square, $8 \times 8$: first four rows of band $a$ (lowpass with decimation), then four rows of band $d$ (highpass with decimation), as on the "Matrix" tab with the row order set to "blocks". Rows and columns are numbered from zero. Find the entry $A_{ij}$ in row $i = {{r}}$ and column $j = {{c}}$. The answer is a number; expressions such as `sqrt(2)/2` are accepted.

## Hints {#hints}

1. Column $j$ of the matrix is the response of the bank to a unit impulse at position $j$, that is the coefficients $a$ and $d$ of the signal $e_j$. Open the "Matrix" tab, pick Haar and $N = 8$; see {#fbm-infinite}.
2. Row $k$ of band $a$ takes the pair of samples $x[2k], x[2k+1]$; row $k$ of band $d$ takes the same pair. A row $i \ge 4$ belongs to band $d$ with index $k = i - 4$.
3. In band $a$ both nonzero entries of a row equal $\tfrac{1}{\sqrt2}$; in band $d$ they have opposite signs. Which sign stands in the column you need is shown by the tab or by running the bank on an impulse.

## Solution {#solution}

$A_{ij} = {{expected}}$ for $i = {{r}}$, $j = {{c}}$. Every row of the Haar $A$ has just two nonzero entries of magnitude $\tfrac{1}{\sqrt2}$ and the rest are zeros: filters of length 2 cover one pair of neighbouring samples, and row $k$ ($k = i$ for $a$, $k = i - 4$ for $d$) looks at columns $2k$ and $2k+1$. The matrix is orthogonal, and its block structure, the "double shift", is described in {#fbm-infinite} and {#fbm-periodization}.
