# How many bits per coefficient

The signal `x` is `gen` with `kind=doppler`, `rate={{n}}`, `duration=1`. The start graph decomposes it with `dwt` (db4, 5 levels), quantizes the coefficients with `coeff_quantize` (node `q`, `mode=bits`) and rebuilds; the output is node `y`. Now `bits=16`: the quality has a margin, and there are too many bits. Find the smallest number of bits `bits` for which the $\mathrm{SNR}$ of the output $y$ relative to `x` is at least 40 dB. Do not change nodes `x` and `w`.

## Goals {#goals}

1. $\mathrm{SNR}(y, x) \ge 40$ dB.
2. The number of bits `q.bits` is at most 6.
3. Every path from `x` to `y` goes through `coeff_quantize`.

## Hints {#hints}

1. Change only the parameter `bits` of `q` and watch the SNR: every bit adds about 6 dB.
2. If the SNR is below 40 dB, the bits are not enough; if it is far above, you can lower it.
3. The minimum is the value at which the SNR is still not below 40 dB, and with one bit fewer it already is.

## Solution {#solution}

The reference solution: `bits` = {{m.bits}}, $\mathrm{SNR}$ = {{m.snr}} dB.

Uniform quantization with a step $\Delta$ adds an error of power about $\Delta^2/12$. The step in each band is `max|c| / (2^(bits-1) - 1)`, so an extra bit halves the step and raises the SNR by about 6 dB. Choosing the number of bits is choosing a point on the rate–distortion curve ({#audio-compression}).
