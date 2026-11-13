# Moving average as a lowpass

The input is a constant component {{c}} plus a 1500 Hz tone with amplitude {{a}} at a sampling rate of 8000 Hz. Do not change nodes `t` and `x`. The output `y` must attenuate the tone by at least 20 dB while the constant component passes almost unchanged. The start graph averages over just two samples, and the tone is barely attenuated.

Choose the length of the averaging window. Ready-made lowpass filters (`iir`, `fir`, `convolve` and similar) are forbidden.

## Goals {#goals}

1. The gain of the 1500 Hz tone at `y` relative to `x` is at most −20 dB.
2. The gain of the constant component is at least −1 dB.
3. The graph has no ready-made lowpass filters, resamplers or wavelets.

## Hints {#hints}

1. A moving average over $M$ samples is an FIR filter with taps $1/M$, see {#lti-frequency-response}. Averaging a constant returns the same constant.
2. The magnitude response is $\left|\dfrac{\sin(\pi f M / f_s)}{M \sin(\pi f / f_s)}\right|$. It vanishes when $f M / f_s$ is an integer. Which numbers of samples $M$ make it an integer for $f = 1500$ Hz and $f_s = 8000$ Hz?
3. Do not take the window too long: until it fills up the output is too low and the constant component is lost. A dozen or two dozen samples will do.

## Solution {#solution}

`moving_average(length=40)` is enough. At 1500 Hz a window of 40 samples gives $|H| \le 1/(40 \sin(\pi \cdot 1500/8000)) \approx 0.045$, that is −27 dB or better. The constant component passes with gain 1, since the mean of equal numbers is that number; there is only a small loss over the first 39 samples while the window fills with zeros. A window that is too short will not attenuate the tone, one that is too long eats the constant component at the start. This is the simplest lowpass: its response is a set of lobes and is far from ideal, see {#lti-frequency-response}.
