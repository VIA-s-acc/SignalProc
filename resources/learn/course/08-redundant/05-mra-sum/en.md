# MRA components add up to the signal

Multiresolution analysis (MRA) returns every band of a decomposition to the time domain: the `mra` block with `component=d1` gives the contribution of the first-level details as a signal of the same length. Build the MRA of the signal `x = [{{x}}]`: after `dwt` (Haar, 3 levels, node `w`) take the components of all the bands — $a_3, d_3, d_2, d_1$ — and add them with `add` blocks. The sum is node `sum`; it must equal `x`. Do not change nodes `x` and `w`. The start graph takes only $d_1$.

## Goals {#goals}

1. The sum equals the input: $\max|\mathrm{sum} - x| \le 10^{-12}$ with no delay — on your signal and on the hidden ones.
2. The graph has at least four `mra` blocks.
3. Every path from `x` to `sum` goes through `mra`.
4. No inverse-transform blocks (`idwt`, `iswt` and the like).

## Hints {#hints}

1. `add` has two inputs, `a` and `b`; four components need three adders, for example $(a_3 + d_3) + (d_2 + d_1)$.
2. The `mra` block chooses a band with the `component` parameter: `a3`, `d3`, `d2`, `d1`. All four take their input from `w`.
3. If a band is missing, the error equals exactly the contribution of that band: check that all four are connected.

## Solution {#solution}

The error of the reference solution: {{m.err}}.

The decomposition is linear, and each component is the synthesis from one band with the others set to zero. So the sum $a_3 + d_3 + d_2 + d_1$ is the synthesis from all bands, that is the signal itself. This is MRA: the signal splits into terms independent in scale, each visible in time, and they can be edited separately ({#wav-mra}). Remove one, and its scale vanishes from the signal.
