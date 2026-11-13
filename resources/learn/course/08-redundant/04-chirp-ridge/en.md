# Chirp frequency at a given moment

A linear chirp: `chirp` with `method=linear`, the frequency rises from $f_0 = {{f0}}$ Hz to $f_1 = {{f1}}$ Hz over 1 s at `rate=1000`. At what frequency, in hertz, does it sound at the moment $t = {{k}}$ ms (sample number {{k}})? Read it from the ridge of the synchrosqueezed CWT: the `sst_ridge` block with `lo=30`, `hi=480`, output `freq`. The tolerance is 2 %.

## Hints {#hints}

1. Build `chirp → sst_ridge` and open the probe on the port `freq`: it is the instantaneous frequency of the strongest component at every sample.
2. The sample you need is number {{k}}; the `cut` block with `begin={{k}}` and `end` one larger leaves a single sample.
3. Compare with the formula: for a linear chirp the frequency grows uniformly, $f(t) = f_0 + (f_1 - f_0)\,t / T$.

## Solution {#solution}

The ridge gives {{expected}} Hz.

The formula gives $f_0 + (f_1 - f_0)\cdot {{k}}/1000$ with $f_0 = {{f0}}$ and $f_1 = {{f1}}$: a linear law is the same increment in hertz per equal time. The CWT ridge finds it without a formula, as the position of the maximum on the scalogram at every moment; synchrosqueezing pulls a blurred maximum into a thin line, and the frequency reads more exactly ({#cwt}, {#synchrosqueezing}). Near the very edges of the signal the ridge is less reliable, so the moment is taken from the middle.
