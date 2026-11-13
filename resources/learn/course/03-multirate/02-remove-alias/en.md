# Remove aliasing before ↓2

The signal `x` is the sum of two tones at a sampling rate of 8000 Hz: a low one, 500 Hz, and a high one, 3000 Hz. After ↓2 the new sampling rate is 4000 Hz, the band edge is 2000 Hz, and the high tone folds onto 1000 Hz. The start graph has a `downsample` with `anti_alias=no`, without a filter. The output is node `y`, which follows the ↓2. Do not change nodes `lo`, `hi` and `x`.

Make the folded copy in `y` at least 40 dB weaker than the low tone, while the low tone itself barely suffers.

## Goals {#goals}

1. The level of the copy at 1000 Hz in `y` is at most the level of the 500 Hz tone in `y` minus 40 dB.
2. The 500 Hz tone loses at most 1 dB compared with `lo`.
3. Every path from `x` to `y` goes through decimation ↓2.

## Hints {#hints}

1. Aliasing happens at ↓2 itself, so the high tone has to be removed before it: with a lowpass filter that passes 500 Hz and suppresses 3000 Hz.
2. A `fir` block with a cutoff between the two tones, say 1800 Hz, in front of `downsample` will do. You can also set `anti_alias=yes`: then the filter is built into the block.
3. Weakening the whole signal with a gain does not help: the goal compares the copy with the low tone, and the low tone must not be lost.

## Solution {#solution}

The graph: `x → fir(numtaps=101, cutoff=1800) → downsample(m=2, anti_alias=no) → y`.

After ↓2 a tone of frequency $f$ above the edge $f_s/4$ is indistinguishable from the tone $f_s/2 - f$: 3000 Hz turns into 1000 Hz. Once aliasing has happened, the copy cannot be told from a genuine component, so the lowpass with cutoff $f_s/(2M)$ goes before the decimation. The level of the copy in the reference solution is {{m.image}} dB with the low tone at {{m.low}} dB. See {#multirate-anti-alias}.
