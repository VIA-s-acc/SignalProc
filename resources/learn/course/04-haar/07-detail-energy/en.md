# The share of energy in the details

One level of the Haar bank (filters $h_0, h_1$, $\downarrow 2$ with `phase=1`) split the signal `x = [{{x}}]` into the approximation $a$ and the details $d$. What share of the total energy $\sum a^2 + \sum d^2$ is in the details? The answer is a number in percent; tolerance 0.1 (absolute, in percent).

## Hints {#hints}

1. The Haar bank is orthogonal, so by Parseval ({#met-parseval}) $\sum a^2 + \sum d^2 = \sum x^2$: the denominator can be taken from the signal itself.
2. The energy of the details: $\sum d^2 = \tfrac12\sum_n (x[2n+1] - x[2n])^2$.
3. The share is $100\,\sum d^2 / \sum x^2$. A check: the shares of $a$ and $d$ add up to 100 %.

## Solution {#solution}

The energy of the approximation is {{m.ea}}, of the details {{m.ed}}; the share of the details is {{expected}} %.

The orthogonality of the bank preserves energy: it is only redistributed between the bands. For a signal that changes slowly from sample to sample the details are almost zero; for a noise-like one their share is about a half. This share decides which coefficients can be dropped in compression and denoising ({#haar-tradeoff}).
