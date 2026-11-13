# A long recording

A 4 s "recording" at $f_s = 8000$ Hz: harmonics `x` with a fundamental of 220 Hz and noise `w` (white, RMS {{a}}), node `n` adds them. That is 32000 samples, more than ten times as many as in the earlier tasks of the chapter. The chain starts from the noisy `n`, the clean `x` is only the reference. The output is node `y`; for now it repeats `n`. Do not change the nodes `x`, `w` and `n`.

Build a "DWT → threshold → inverse DWT" denoiser and gain at least 5 dB of SNR over the input without silencing the fundamental.

## Goals {#goals}

1. The SNR of `y` relative to `x` is higher than the SNR of `n` by at least 5 dB.
2. The 220 Hz fundamental on `y` is weaker than on `x` by less than 1 dB.
3. Every path from `n` to `y` goes through a `threshold` block.
4. The clean signal `x` reaches the output only through the noisy recording `n`: it cannot be mixed in around it.

## Hints {#hints}

1. The chain is the same as in task 10.2: `dwt`, `threshold`, `idwt`. The number of levels can stay `0`: the block picks the maximum for the signal length minus three; see {#audio-denoising}.
2. The choice of rule matters here. The `visu` rule uses the threshold $\sigma\sqrt{2\ln N}$, which grows with the record length $N$, and at 32000 samples it cuts the harmonics as well as the noise.
3. If the second goal fails while the SNR grows, the threshold is too large for the tone: switch the rule to an adaptive one.

## Solution {#solution}

The graph: `n → dwt(sym8, levels=0) → threshold(bayes, soft) → idwt → y`. The SNR at the input is {{m.in}} dB, at the output {{m.out}} dB; the fundamental stays in place to within {{m.f0}} dB. The noise is white and spread evenly over all bands, while the energy of the harmonics is gathered in a few large coefficients, so the threshold removes the noise without touching the tones. The universal `visu` threshold grows as $\sqrt{2\ln N}$: the longer the record, the more it suppresses weak harmonics. On this record it gives an output worse than the input and weakens the fundamental by several dB. Details in {#audio-denoising}.
