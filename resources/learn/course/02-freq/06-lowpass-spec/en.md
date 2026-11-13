# Lowpass to a spec

The input `x` is the sum of two tones at $f_s = 8000$ Hz: the wanted one at $f_1 = 500$ Hz and an interference at $f_2 = 3000$ Hz with amplitude {{a2}}. Build a filter whose output is the node `y`: the wanted tone must lose less than 1 dB, the interference at least 40 dB. Do not change the sources `g1`, `g2` and the adder `x`. Any filtering blocks are allowed.

## Goals {#goals}

1. The tone $f_1 = 500$ Hz on `y` is weaker than on `x` by less than 1 dB.
2. The tone $f_2 = 3000$ Hz on `y` is suppressed by at least 40 dB relative to `x`.

## Hints {#hints}

1. You need a lowpass with a cutoff between $f_1$ and $f_2$. The `fir` block (window method) sets the cutoff in hertz with the `cutoff` parameter; see {#cls-window-method}.
2. The window sets the depth of suppression: Hamming gives about 53 dB, Hann about 44 dB, rectangular only 21 dB. The number of taps `numtaps` sets the transition width.
3. The interference must fall into the stopband, not the transition region: compare $f_2 - \text{cutoff}$ with the transition width ({#freq-band-metrics}). Simply muting the input (`gain` at −60 dB) will not do: the wanted tone disappears too.

## Solution {#solution}

`fir(numtaps=101, cutoff=1500, window=hamming)` works. Levels measured on the reference: wanted tone {{m.pass}} dB, interference {{m.stop}} dB. The 1500 Hz cutoff lies midway between the tones, the transition region of a 101-tap filter at $f_s = 8000$ Hz is about 250 Hz wide and reaches neither of them, and the Hamming window gives about 53 dB of suppression, more than the required 40. The filter has linear phase ({#lti-linear-phase}), so it only delays the output by 50 samples without distorting the tones. Details in {#freq-magnitude}.
