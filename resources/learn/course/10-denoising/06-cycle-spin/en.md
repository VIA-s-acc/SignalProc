# Averaging over shifts

The graph holds a clean signal `x` (Bumps) and a noisy `n` at SNR {{snr}} dB. Node `y1` is a Haar BayesShrink denoiser with a single shift; it is the reference for comparison and stays as it is. The output is node `y`: for now it is preceded by the same one-shift `denoise`, so there is no gain. Do not change the nodes `x`, `n` and `y1`.

The Haar wavelet is not shift invariant: the result depends on where the grid starts. Average the results for several shifts of the signal (cycle spinning) so that the SNR of `y` is higher than that of `y1` by at least 0.5 dB.

## Goals {#goals}

1. The SNR of `y` relative to `x` is higher than the SNR of `y1` by at least 0.5 dB.
2. Every path from `n` to `y` goes through a `denoise` block.

## Hints {#hints}

1. The `denoise` block can average over shifts itself: the `shifts` parameter; see {#audio-denoising}.
2. You need several shifts but not many: the gain saturates quickly. Start with 4 to 8.
3. The same can be built by hand: several shifted copies of `n`, a one-shift `denoise` for each, the inverse shift and the average. But a `denoise` block with `shifts` is simpler.

## Solution {#solution}

The graph: `n → denoise(haar, levels=5, bayes, soft, shifts=8) → y`. The SNR of the reference `y1` is {{m.single}} dB, of `y` {{m.multi}} dB. The artefacts of the threshold are tied to the decimation grid and fall in different places for different shifts of the signal, while the signal itself coincides after the inverse shift. The average over shifts therefore keeps the signal and suppresses the artefacts. In trial runs with different noise `shifts=2` gave a gain of 1.3 to 2.2 dB, `shifts=8` 1.9 to 2.8 dB, and `shifts=16` added almost nothing to eight. Details in {#audio-denoising}.
