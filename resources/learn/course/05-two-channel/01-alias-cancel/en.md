# Which synthesis removes aliasing

A two-channel bank with analysis filters $H_0, H_1$ and synthesis filters $F_0, F_1$ gives $\hat X(z) = T(z)X(z) + A(z)X(-z)$, where $A(z) = \tfrac12\left[F_0(z)H_0(-z) + F_1(z)H_1(-z)\right]$ ({#fb-distortion-alias}). Aliasing vanishes when $A \equiv 0$ for any $H_0, H_1$. Which choice of synthesis achieves this? Enter the option number (1–4) or its text.

## Hints {#hints}

1. Substitute each option into $A(z)$ and see whether the two terms cancel for arbitrary $H_0, H_1$.
2. $A$ contains $H_0(-z)$ and $H_1(-z)$. For the terms to cancel, $F_0$ must be tied to $H_1(-z)$ and $F_1$ to $H_0(-z)$, with opposite signs.
3. A check on Haar: in the program $h_0 = [1, 1]/\sqrt2$ and $f_1 = [-1, 1]/\sqrt2$, that is $F_1(z) = -H_0(-z)$.

## Solution {#solution}

Option {{expected}} is right: $F_0(z) = H_1(-z)$, $F_1(z) = -H_0(-z)$.

Then $A = \tfrac12\left[H_1(-z)H_0(-z) - H_0(-z)H_1(-z)\right] = 0$ for any filters: aliasing disappears because of the choice of synthesis, not because of a property of the analysis. In option 3 the terms add instead of subtracting; in options 2 and 4 the indices are mixed up and the terms differ. This is the standard choice of {#fb-alias-cancellation}: one condition remains, on $P_0 = F_0 H_0$.
