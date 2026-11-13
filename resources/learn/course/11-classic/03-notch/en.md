# Remove 50 Hz mains hum

The signal `x` is the sum of a 440 Hz tone and a 50 Hz mains hum of amplitude {{a}} at $f_s = 8000$ Hz. Build a filter whose output is node `y`: the hum must go and the tone must stay. Do not change the nodes `tone`, `hum` and `x`.

## Goals {#goals}

1. The 50 Hz hum on `y` is weaker than on `x` by at least 30 dB.
2. The 440 Hz tone on `y` is weaker than on `x` by less than 1 dB.

## Hints {#hints}

1. The hum lies below the tone, so a highpass with a cutoff between them could work, but to remove 30 dB at 50 Hz and spare 440 Hz it has to be steep. A notch removes exactly one frequency.
2. A `biquad` block of type `notch` puts a zero of the magnitude response exactly at `f0`; the quality factor `q` sets the width of the dip: the larger, the narrower; see {#cls-allpass-notch}.
3. A narrow dip hardly touches 440 Hz, a wide one (small `q`) will. Try `q` around 10.

## Solution {#solution}

`biquad(type=notch, f0=50, q=10)` works. Measured on the reference: hum {{m.hum}} dB, tone {{m.tone}} dB. The zeros of a notch section lie on the unit circle at 50 Hz, so a sinusoid of exactly this frequency is cancelled almost completely, and the width of the dip at the $-3$ dB level is $f_0/q = 5$ Hz, which does not reach 440 Hz. The poles sit next to the zeros and cancel the dip outside it. Details in {#cls-allpass-notch}.
