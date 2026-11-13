# Lossless compression: integer 5/3 lifting

Build a lossless codec: `lift53` (node `l`, 3 levels) → `ilift53` (node `i`) → output `y`. The signal is `x = [{{x}}]`. The reconstruction must be exact: $y = x$ exactly, not "up to $10^{-12}$". The start graph uses real-valued lifting; for arbitrary numbers it leaves rounding errors. Do not change node `x`: the check feeds other integer signals into it too.

## Goals {#goals}

1. The reconstruction error is zero: $y = x$ — on your signal and on the hidden ones.
2. The coefficients `l.a` equal the reference, integer ones (in the integer mode they are scaled by $2^{15}$).
3. Every path from `x` to `y` goes through `lift53`.
4. Every path from `x` to `y` goes through `ilift53`.

## Hints {#hints}

1. The integer lifting mode is switched on by the parameter `integer` of `lift53`.
2. The real-valued mode may also restore small integers exactly but does not guarantee it: the check tells the modes apart by the coefficients `l.a`.
3. The inverse block `ilift53` has no parameters: it learns the mode from the coefficients.

## Solution {#solution}

The error of the reference solution: {{m.err}}.

Lifting builds a wavelet transform from "predict" and "update" steps, and each step can be inverted by repeating it with the opposite sign. If every step is rounded to an integer, the transform stays invertible: what was added is subtracted by the same rounded number. So the integer 5/3 of JPEG 2000 reconstructs without loss, unlike the real-valued one, where the error depends on the arithmetic ({#lift-53}, {#lifting}). The mode `integer=yes` turns the samples into integers ×32768, as for 16-bit sound.
