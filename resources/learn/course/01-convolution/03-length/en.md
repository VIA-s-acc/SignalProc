# Length of the full convolution

The signal $x$ has $N = {{N}}$ samples, the impulse response of the filter $h$ has $L = {{L}}$. How many samples does the full (linear) convolution $y = x * h$ have? The answer is an integer.

## Hints {#hints}

1. Full convolution is multiplication by the Toeplitz matrix $T(h)$. How many columns and how many rows does it have? See {#toeplitz-convolution}.
2. The first nonzero sample of $y$ is $y[0] = h[0]x[0]$. Where does the last product, the one with $h[L-1]$ and $x[N-1]$, sit?
3. A check on small numbers: a signal of one sample and a filter of $L$ samples give a convolution of $L$ samples.

## Solution {#solution}

The length is $N + L - 1 = {{expected}}$. The last nonzero product $h[L-1]\,x[N-1]$ sits at position $N - 1 + L - 1$, and numbering starts at zero, so there is one more sample than that: $N + L - 1$. The filter slides into the signal and out of it, which gives $L - 1$ extra samples compared with the input. The `fir_taps` block cuts the result to $N$, while the full convolution cuts nothing; more in {#lti-convolution}.
