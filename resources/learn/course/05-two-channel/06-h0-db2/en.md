# h₀ of the orthogonal db2 split

Among the 20 splits of $P_0$ of degree 6 there are two orthogonal ones, mirror images of each other, with $H_0$ and $F_0$ of length 4. Find the taps of the db2 analysis filter $h_0$ in the order the program stores them (`db2.h0`): four numbers in time order, the first negative. The normalisation is $\sum h_0 = \sqrt2$, $\sum h_0^2 = 1$. Compare with `signals filter info db2`. Tolerance $10^{-9}$.

## Hints {#hints}

1. $H_0$ has two zeros at $z = -1$: $H_0 \propto (1 + z^{-1})^2 (1 - s z^{-1})$ with one root $s$ from the pair $\{r, 1/r\}$, $r = 2 - \sqrt3$.
2. For an orthogonal bank $\sum h_0[n]h_0[n+2k] = \delta_k$ ({#fb-cqf}): with $\sum h_0 = \sqrt2$ the sum of squares is 1, and $h_0[0]h_0[2] + h_0[1]h_0[3]$ is zero.
3. Every tap contains $\sqrt3$ to the first power: solve the system given by the sum and the orthogonality conditions. If you get the mirror solution (the first tap positive), write it in reverse order.

## Solution {#solution}

$h_0 = [{{expected}}]$.

This is $\dfrac{1}{4\sqrt2}\left(1 - \sqrt3,\ 3 - \sqrt3,\ 3 + \sqrt3,\ 1 + \sqrt3\right)$. The sum: $\tfrac{8}{4\sqrt2} = \sqrt2$; the sum of squares: 1; $h_0[0]h_0[2] + h_0[1]h_0[3] = 0$. The second orthogonal split is the same filter written in reverse order; it is not accepted as the answer.
