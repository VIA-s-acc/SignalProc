# How many splits has P₀ of degree 6

The maximally flat halfband polynomial $P_0$ with $p = 2$ has degree 6. Its roots are distributed between $H_0$ and $F_0$ in every admissible way; each such split gives a bank with perfect reconstruction ({#fb-p0-factorisations}). How many splits does $P_0$ have for $p = 2$? Open the Designer and choose $p = 2$, or run `signals design --p 2`. The answer is a whole number.

## Hints {#hints}

1. The roots of $P_0$: a fourfold root $z = -1$ and two real roots $r$ and $1/r$, where $r = 2 - \sqrt3$.
2. The roots at $-1$ are indistinguishable: only the number of them that went to $H_0$ matters (from zero to four). The roots $r$ and $1/r$ are distinguishable.
3. The number of splits is the number of ways for the roots at $-1$ times the number of ways for $r$ and $1/r$. Compare with the number in the heading of the Designer list.

## Solution {#solution}

The number of splits is {{expected}}.

$H_0$ can get 0, 1, 2, 3 or 4 roots at $-1$: five ways. Each of the roots $r$ and $1/r$ goes to $H_0$ or to $F_0$: $2 \cdot 2 = 4$ ways. In all $5 \cdot 4 = 20$, and `signals design --p 2` prints the same 20 rows. Only 6 of them give wavelets: both filters need a zero at $-1$ and a smoothness exponent above zero ({#fb-p0-factorisations}).
