# Group delay of a symmetric FIR

An FIR filter of length $L = {{L}}$ is symmetric: $h[k] = h[L-1-k]$, so its phase is linear. What is its group delay in samples? The answer is a whole number.

## Hints {#hints}

1. For a symmetric filter of length $L$ the centre of symmetry is sample number $(L-1)/2$, see {#lti-linear-phase}.
2. The phase $\varphi(\omega) = -\omega\,\tau$ is linear, and the group delay $\tau = -\,d\varphi/d\omega$ does not depend on frequency ({#freq-group-delay}).
3. Check on a graph: an impulse through `fir` with `numtaps = {{L}}` and `compensate = no` — where is the peak of the output?

## Solution {#solution}

The group delay is $(L-1)/2 = {{expected}}$ samples. The impulse response is symmetric about sample {{expected}}, and the plot shows its maximum there: a linear-phase filter just shifts the signal by half its length without changing the shape. For an even length the shift would be a half-integer, which is why $L$ is odd here.
