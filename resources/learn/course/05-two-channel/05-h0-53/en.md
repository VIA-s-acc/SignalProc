# h₀ of the linear-phase 5/3 split

Among the 20 splits of $P_0$ of degree 6 there is a linear-phase one with $H_0$ of length 5 and $F_0$ of length 3 — the 5/3 wavelet (`bior2.2` in the program). Find the taps of this analysis filter $h_0$: five numbers in time order, symmetric about the middle. The normalisation is that of the program's filters: $\sum h_0 = \sqrt2$ (in the Designer and in the book table $H_0$ is normalised differently, $H_0(1) = 1$, so the taps there are $\sqrt2$ times smaller). Compare with `signals filter info bior2.2`: its $h_0$ has an extra zero sample on the left, which is not part of the answer. Tolerance $10^{-9}$.

## Hints {#hints}

1. $H_0$ has two zeros at $z = -1$, that is, it is divisible by $(1 + z^{-1})^2$, and the remaining factor in the 5/3 split is a polynomial of degree two with the roots $r$ and $1/r$.
2. The unnormalised $H_0 \propto (1 + z^{-1})^2 (-1 + 4z^{-1} - z^{-2})$: multiply out to get integer taps.
3. Divide the integer taps by their sum and multiply by $\sqrt2$. The answer may use expressions such as `3*sqrt(2)/4`.

## Solution {#solution}

$h_0 = [{{expected}}]$.

$(1 + z^{-1})^2(-1 + 4z^{-1} - z^{-2}) = -1 + 2z^{-1} + 6z^{-2} + 2z^{-3} - z^{-4}$; the taps add up to 8. With $H_0(1) = 1$ this gives $(-1, 2, 6, 2, -1)/8$ — the row of the table in {#fb-p0-factorisations}; with $\sum h_0 = \sqrt2$ the same numbers times $\sqrt2$, that is $\tfrac{\sqrt2}{8}(-1, 2, 6, 2, -1)$. The filter is symmetric, hence the phase is linear.
