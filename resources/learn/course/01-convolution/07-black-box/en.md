# Impulse response of a black box

The start graph is small: the signal `x` goes along two branches that are added, and node `y` keeps the first 6 samples. The graph is linear and time-invariant, so it is fully described by its impulse response $h[n]$, the response to the unit impulse $\delta = [1\ 0\ 0\ \dots]$.

Find the first 6 samples $h[0], \dots, h[5]$ of this graph. The answer is 6 numbers separated by spaces.

## Hints {#hints}

1. The most direct way: replace the values of node `x` by `1 0 0 0 0 0 0 0`, run the graph (`r`) and read probe `y`.
2. It can be done without running: the response of a sum is the sum of the responses of the branches. What is the response of `fir_taps` with taps `1 {{a}}`, and of the chain "delay, multiply"?
3. If both responses fall on the same index, they add.

## Solution {#solution}

$h = [{{expected}}]$. The upper branch is `fir_taps` with taps `1 {{a}}`: its response to an impulse is the taps themselves, that is $1$ at zero and ${{a}}$ at position 1. The lower branch delays the impulse by $d = {{d}}$ and multiplies by $k = {{k}}$: response {{k}} at position {{d}}. The adder adds the responses sample by sample, and where positions coincide the values sum. For a linear time-invariant system this is enough: the response to any input is the convolution with $h$, see {#lti-convolution}.
