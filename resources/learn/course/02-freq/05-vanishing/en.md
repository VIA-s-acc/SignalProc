# Vanishing moments of db{{n}}

How many zeros does the lowpass $h_0$ of the wavelet `db{{n}}` have at $z = -1$ (at $\omega = \pi$)? That is also its number of vanishing moments. The answer is a whole number.

## Hints {#hints}

1. The filter `db{{n}}.h0` has the form $H_0(z) = \left(\tfrac{1 + z^{-1}}{2}\right)^{p} Q(z)$ with $Q(-1) \ne 0$. You need the power $p$, see {#wav-vanishing-moments}.
2. The number of vanishing moments is the number of zeros at $\omega = \pi$: the magnitude response vanishes there, the faster the more zeros.
3. The command `signals filter info db{{n}}` prints a line with the zeros at $z = -1$. Compare the filter length with the number of zeros.

## Solution {#solution}

`db{{n}}` has exactly {{expected}} zeros at $z = -1$ (and as many vanishing moments). The filter length is $2N = 2{{expected}}$ while the zeros number $N$: a Daubechies wavelet of order $N$ spends half of its degrees of freedom on zeros at $z = -1$, the rest go to orthogonality. Details in {#wav-vanishing-moments}.
