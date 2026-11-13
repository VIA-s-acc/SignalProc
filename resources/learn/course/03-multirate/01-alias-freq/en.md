# The alias frequency

A tone of frequency $f_0 = {{f0}}$ Hz is sampled at $f_s = 9000$ Hz and decimated by $M = {{m}}$ without a filter: every $M$-th sample is kept. At what frequency, in hertz, does the tone appear in the decimated signal? Tolerance 1 Hz.

## Hints {#hints}

1. After decimation the sampling rate is $f_s / M$, and only frequencies from 0 to half of it can be told apart.
2. A tone above this limit folds back: its frequency is defined up to a multiple of $f_s / M$. Subtract from $f_0$ the multiple that brings the result into $[0,\; f_s/(2M)]$ (the sign can be dropped).
3. A check: generate the tone $f_0$ on the "Signal" tab and decimate it with a `downsample` block set to `anti_alias=no`; the spectrum peak gives the answer.

## Solution {#solution}

The answer: {{expected}} Hz.

The new sampling rate is $f_s / M$, and the spectrum of the decimated signal is the original spectrum continued periodically with that period: $f_0$ is indistinguishable from $f_0 - k f_s/M$. Of these values the one in $[0,\; f_s/(2M)]$ in absolute value is taken. More on aliasing in {#multirate-anti-alias} and {#multirate-down-up}.
