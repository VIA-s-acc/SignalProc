# Decimation, then expansion

The signal `x = [{{x}}]` goes through ↓2 and then ↑2, both blocks without filters. ↓2 keeps the even-numbered samples $x[0], x[2], \dots$ (phase 0, the default of the `downsample` block); ↑2 inserts a zero after every sample. Write the result: 8 numbers separated by spaces.

## Hints {#hints}

1. After ↓2 four samples remain: $x[0], x[2], x[4], x[6]$.
2. ↑2 puts them on even places and zeros on odd ones: $y[2n] = x[2n]$, $y[2n+1] = 0$.
3. A check: the odd-numbered samples of the original are lost for good.

## Solution {#solution}

$y = [{{expected}}]$.

In the frequency domain $Y(z) = \tfrac12\left[X(z) + X(-z)\right]$: the mirror copy of the signal is added, and the odd samples are erased. This ↓2, ↑2 pair stands in every branch of the filter bank of {#multirate-down-up}; {#conv-pywt} describes how the decimation phase shifts the result.
