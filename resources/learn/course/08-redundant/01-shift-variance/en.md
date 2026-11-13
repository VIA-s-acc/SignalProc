# Detail energy under an input shift

The signal `x = [{{x}}]` goes through one level of the Haar DWT (`dwt`, `levels=1`, `ext=periodization`; ↓2 with phase 1, as in PyWavelets). Then the same signal, delayed by one sample circularly (`shift`, `samples=1`, `mode=circular`: the last sample moves to the start), goes through the same decomposition. What percentage of the energy of the details $d_1$ of the original is the energy of the details $d_1$ of the shifted signal? The answer is a number in percent; the tolerance is 0.1 (absolute). A decimal comma is accepted.

Energy is the sum of squared samples of the band. The shift does not change the energy of the signal itself, so the whole difference comes from the decomposition.

## Hints {#hints}

1. A Haar detail is the difference of the two samples of a pair: $d_1[n] = \pm\tfrac{1}{\sqrt2}(x[2n+1] - x[2n])$. The energy is $\sum d_1^2 = \tfrac12\sum_n (x[2n+1]-x[2n])^2$.
2. After the delay the pairs are the neighbours $x[2n-1]$ and $x[2n]$; the first pair is $x[15]$ and $x[0]$ (the shift is circular).
3. A check with no hand work: if the decomposition did not depend on the shift, the answer would be 100 %. Build two `dwt` blocks on the "Pipeline" tab, square $d_1$ and add up with `cumsum`: the last sample is the energy.

## Solution {#solution}

The energy of $d_1$ of the original is {{m.e1}}, of the shifted signal {{m.e2}}; the ratio is {{expected}} %.

The decimated DWT is not shift invariant: after ↓2 the samples of one parity remain, and a shift by one puts other neighbours into the pairs. The details are differences of other pairs of numbers, so their energy and the whole picture of coefficients change. Further on, `swt` drops the decimation, and a shift of the input just shifts the coefficients ({#swt-modwt}, {#transforms}).
