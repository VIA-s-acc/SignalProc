# Haar: coefficients d by hand

The same analysis step on the detail branch: the signal `x = [{{x}}]` goes through $h_1$ (full convolution, $h_1 = [\tfrac{1}{\sqrt2}, -\tfrac{1}{\sqrt2}]$ — exactly this sign, as in the program), then $\downarrow 2$ with `phase=1`. Find the detail coefficients $d$. The answer is a vector; tolerance $10^{-9}$.

## Hints {#hints}

1. The full convolution with $h_1$ gives $y[k] = \tfrac{1}{\sqrt2}\,(x[k] - x[k-1])$.
2. `phase=1` keeps $y[1], y[3], y[5], y[7]$, so $d[n]$ depends on $x[2n+1]$ and $x[2n]$.
3. The sign matters: take the odd sample minus the even one, not the other way round. Compare with `signals filter info haar`.

## Solution {#solution}

$d = [{{expected}}]$.

$d[n] = (x[2n+1] - x[2n])/\sqrt2$: the difference of a neighbouring pair. It is small on a smooth stretch and large at a jump, so the details carry edges and noise ({#haar-filters}). Together with $a$ from task 4.1 the pair $(a[n], d[n])$ is a rotation of the pair $(x[2n], x[2n+1])$ in the plane, and the sum of squares does not change.
