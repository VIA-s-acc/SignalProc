# The noble identity

Branch A: the filter $H(z^2)$ at the full rate, then ↓2. Branch B: first ↓2, then the filter $H(z)$ at the half rate. Here $H(z) = {{h0}} + {{h1}}z^{-1} + {{h2}}z^{-2}$, that is, the taps `h = [{{h0}} {{h1}} {{h2}}]`. The noble identity says the two branches give the same result. Build them so that they agree exactly.

Nodes: in branch A the filter `ha`, then `ya`; in branch B the decimator `db`, then the filter `yb`. The outputs are `ya` and `yb`. Do not change nodes `x` and `yb`: the check feeds other signals into `x`. Use `fir_taps` for the filters and decimation without the built-in lowpass.

## Goals {#goals}

1. The output of filter `ha` equals the response of $H(z^2)$ to `x` to $10^{-12}$ — on your signal and on the hidden ones.
2. The outputs `ya` and `yb` agree to $10^{-12}$ with no shift.
3. The graph has at least two `downsample` blocks.
4. Every path from `x` to `yb` goes through decimation ↓2.

## Hints {#hints}

1. The substitution $z \to z^2$ puts one zero between the taps of the filter: $h_0, h_1, h_2$ become $h_0, 0, h_1, 0, h_2$.
2. Both branches must take the same samples. The decimation `phase` differs between the branches in the start graph: make it equal.
3. If goal 1 holds and goal 2 does not, the problem is in branch B: the filter `yb` cannot be changed, so `db` is the one to fix.

## Solution {#solution}

The graph: `x → fir_taps(taps="{{h0}} 0 {{h1}} 0 {{h2}}") → ↓2 → ya` and `x → ↓2 → fir_taps(taps="{{h0}} {{h1}} {{h2}}") → yb`, both phases 0. The disagreement of the branches in the reference solution: {{m.match}}.

Branch A output: $y_A[n] = \sum_k h[k]\,x[2n - 2k + p]$, where $p$ is the phase. Branch B output: $y_B[n] = \sum_k h[k]\,x_p[n-k]$ with $x_p[n] = x[2n+p]$, which is the same. The phase must be common: with different $p$ the branches take different samples. A filter at the half rate costs half the multiplications, which is why the identity is used to move filters across ↓2. See {#multirate-noble}.
