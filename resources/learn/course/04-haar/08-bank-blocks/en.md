# The bank in your own blocks

The graph holds the two-level Haar bank of task 4.6 built from separate nodes: two equal analysis steps and two equal synthesis steps. Move the analysis step and the synthesis step into blocks of your own (`block`) and build the same bank from calls: each block is called twice. The bank's output is the node `y`; it must return the input `x = [{{x}}]` without distortion. Do not change the node `x`: the check feeds other 16-sample signals into it as well.

## Goals {#goals}

1. Perfect reconstruction: $\max|\hat x - x| \le 10^{-12}$ allowing for the delay — on your signal and on the hidden ones.
2. The graph has a block of its own that is called at least twice.
3. The unfolded graph has at least four `downsample` and four `upsample` blocks.
4. No ready-made wavelet transforms.

## Hints {#hints}

1. An analysis step is $h_0 \to \downarrow 2$ and $h_1 \to \downarrow 2$ from one input: the block has one input `x` and two outputs `a` and `d`. A synthesis step is ↑2, $f_0$, $f_1$ and the sum: two inputs `a`, `d` and an output `y`.
2. The quickest way is to mark the nodes of one step with `m` and collapse them with `b`: the program turns the wires across the border into ports itself. Add the second call from the `a` list — the block is in the group of your blocks there. The rules of writing blocks are in {#sig-own-blocks}.
3. The wiring: `x → an1`, `an1.a → an2`, the outputs of `an2` into the inner synthesis, its output and `an1.d` into the outer synthesis `y`. The check looks inside calls, so the four ↓2 and four ↑2 are counted on the unfolded graph.

## Solution {#solution}

The reconstruction error of the reference solution: {{m.err}}.

The block `analysis` describes one step $h_0, h_1, \downarrow 2$, the block `synthesis` one step $\uparrow 2, f_0, f_1, +$. A two-level bank is the same steps called twice: the outer analysis splits $x$, the inner one the approximation $a_1$, and synthesis runs in the opposite order. Unfolding turns the calls into exactly the nodes the graph had, so the numbers stay the same ({#haar-multilevel}). The param `w` with the default `haar` lets you call the same blocks with another wavelet without touching the definitions. One big block with the whole bank inside is called only once and fails the goal about two calls.
