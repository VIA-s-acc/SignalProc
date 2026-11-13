# The maximum number of levels

A signal of $N = {{n}}$ samples is decomposed by a multilevel `dwt` with the wavelet `db{{p}}`, whose filter length is $L = 2 \cdot {{p}}$. What is the largest number of levels $J$ the program allows? The answer is a whole number.

## Hints {#hints}

1. Each level shortens the lowpass band roughly by half, and a filter of length $L$ still has to fit into the signal it is applied to. The longer the filter, the sooner the levels run out.
2. If $N = 2^J (L - 1)$, there are exactly $J$ levels. For other $N$ take the integer part: $J_{\max} = \lfloor \log_2 \tfrac{N}{L - 1} \rfloor$; PyWavelets counts the same way, see {#fbm-boundary-modes}.
3. Check yourself in "Decomposition": ask for one level more than you found, and the program reports the allowed range. Or set the number of levels in a `dwt` block by hand.

## Solution {#solution}

The largest number of levels is {{expected}}: $J_{\max} = \lfloor \log_2 (N/(L - 1)) \rfloor$ with $N = {{n}}$ and $L - 1 = 2 \cdot {{p}} - 1$. It depends on the filter length: the longer the filter, the sooner the band becomes comparable to it in length, and the fewer levels there are. The program rejects the next level with a message about the allowed range. A `dwt` block with `levels = 0` takes not the maximum but the maximum minus three (at least one), so the number cannot be read off it. See {#fbm-boundary-modes}.
