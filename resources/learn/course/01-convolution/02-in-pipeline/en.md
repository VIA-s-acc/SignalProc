# Convolution in the pipeline

Make node `y` output the full convolution of the signal `x` with the filter `h = [{{h}}]`: all $N + L - 1 = 10$ samples. The start graph already has a `fir_taps` block with the right taps, but its output is shorter than needed. Do not change node `x`: the check feeds other signals into it too.

## Goals {#goals}

1. `y` equals the full convolution $x * h$ to $10^{-12}$ — on your signal and on the hidden ones.
2. `y` has exactly $N + L - 1 = 10$ samples.

## Hints {#hints}

1. Run the graph (`r`) and look at probe `y` as numbers (`v`): how many samples are there? `F1` on the `fir_taps` node explains what the block does to the length.
2. `fir_taps` cuts the convolution to the input length. So the input has to be made longer beforehand.
3. The `pad` block with the `after` parameter appends zeros to the signal. How many zeros let a filter of length $L$ slide out of the signal completely?

## Solution {#solution}

The graph: `x → pad(after=2) → fir_taps(taps="{{h}}") → y`.

`fir_taps` computes the convolution like `numpy.convolve` and keeps the first $N$ samples: this is how a real causal filter behaves, since it does not know future input. The full convolution needs $L - 1$ more samples of "tail" — the response to the last input samples. Appending $L - 1 = 2$ zeros makes the input $N + L - 1$ long, so the cut takes nothing away. Extra zeros would give extra samples, which is why the second goal checks the length.
