# Convolution by hand

Compute the full (linear) convolution $y = x * h$ of the signal `x = [{{x}}]` with the filter `h = [{{h}}]`. The signal has $N = 4$ samples and the filter $L = 3$, so the answer has $N + L - 1 = 6$ numbers $y[0], \dots, y[5]$ separated by spaces.

## Hints {#hints}

1. By definition $y[n] = \sum_k h[k]\,x[n-k]$, and samples outside $x$ are zero. For example, $y[0] = h[0]\,x[0]$.
2. Convolution is multiplication by the $6 \times 4$ Toeplitz matrix $T(h)$: every column holds a copy of $h$ shifted one row down. The "Matrix" tab shows it; see {#toeplitz-convolution}.
3. A check without recomputing: the samples of $y$ add up to $\left(\sum x\right)\left(\sum h\right)$.

## Solution {#solution}

$y = x * h = [{{expected}}]$.

Every output sample is the dot product of the flipped filter with a window of the signal. The first and the last samples hold one product each: $y[0] = h[0]x[0]$, $y[5] = h[2]x[3]$. The middle ones hold three. That is why the full convolution is $L - 1 = 2$ samples longer than the input: the filter slides into the signal and out of it.
