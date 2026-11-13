# Two Haar levels from blocks

Build a two-level Haar bank from separate blocks: at the second level the approximation of the first is split once more. Analysis: $h_0, h_1$ and $\downarrow 2$ at every step; synthesis: $\uparrow 2$, $f_0, f_1$ and a sum at every step in reverse order. The bank output is node `sum`; it must return the input `x = [{{x}}]` without distortion. Ready-made transforms (`dwt`, `idwt` and the like) do not count. Do not change node `x`: the check feeds other 16-sample signals into it too.

## Goals {#goals}

1. Perfect reconstruction: $\max|\hat x - x| \le 10^{-12}$ allowing for a delay — on your signal and on the hidden ones.
2. Every path from `x` to `sum` goes through decimation ↓2.
3. The graph has at least four `downsample` blocks and four `upsample` blocks.
4. No ready-made wavelet transforms.

## Hints {#hints}

1. The bank of task 4.3 is one level. Copy its branch $h_0 \to \downarrow 2$ and connect its output to a second such bank: to the input of $h_0$ and to the input of $h_1$ of the second level.
2. Every ↓2 and every ↑2 has the built-in lowpass filters off: `anti_alias=no`, `anti_image=no`; `phase=1` on every ↓2.
3. Synthesis goes in reverse order: first rebuild the first-level approximation from $a_2$ and $d_2$ (↑2, $f_0$, $f_1$, sum), and only then feed it to the first-level synthesis together with $d_1$. You need not look for the delay: the check finds it itself ({#haar-multilevel}).

## Solution {#solution}

The reconstruction error of the reference solution: {{m.err}}.

A two-level bank is a one-level bank in which the branch $a_1$ is itself replaced by a one-level bank. Such a replacement keeps perfect reconstruction: the inner bank returns $a_1$ exactly, and the outer one returns $x$ from $a_1$ and $d_1$ ({#haar-multilevel}). A one-level bank also reconstructs the signal, but it has two ↓2 rather than four, and the goal on the block count rejects it; a bank without the branch $d_2$ loses part of the signal.
