# The value of φ(1) for db2

The scaling function $\varphi$ of the wavelet `db2` satisfies the two-scale equation $\varphi(t) = \sqrt2 \sum_k h[k]\,\varphi(2t - k)$ with the normalisation $\int\varphi = 1$ and support $[0, 3]$. Find $\varphi(1)$ to within $10^{-6}$. The answer is a number; expressions such as `sqrt(3)` are accepted.

## Hints {#hints}

1. At the integers the two-scale equation is a linear system for the vector $v_n = \varphi(n)$, $n = 0 \dots 3$: $v = Mv$ with $M_{n,m} = \sqrt2\,h[2n - m]$. You need the eigenvector for the eigenvalue 1; see {#wav-eigenvector}.
2. At the ends of the support $\varphi(0) = \varphi(3) = 0$, and the values at the integers sum to 1 (partition of unity). Two unknowns remain, $\varphi(1)$ and $\varphi(2)$.
3. The cascade algorithm ({#wav-cascade}, key `c` in the bank designer) shows $\varphi$ only approximately and converges slowly: by eye you get two digits. For six digits solve the system exactly.

## Solution {#solution}

$\varphi(1) = {{expected}}$. Put $t = 1$ and $t = 2$ into the two-scale equation with $h = \tfrac{1}{4\sqrt2}\,(1 + \sqrt3,\ 3 + \sqrt3,\ 3 - \sqrt3,\ 1 - \sqrt3)$: this gives two equations in $\varphi(1)$ and $\varphi(2)$, and the condition $\varphi(1) + \varphi(2) = 1$ picks the right solution. The other value is $\varphi(2) = (1 - \sqrt3)/2 \approx -0.366$: the function takes negative values too. The cascade from a unit delta gives about 1.32 after ten steps, so it tends to the same number but slowly (the smoothness of `db2`'s $\varphi$ is low). More in {#wav-eigenvector}.
