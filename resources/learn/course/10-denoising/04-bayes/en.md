# BayesShrink

The graph holds a clean signal `x` (Bumps), a noisy `n` at SNR 2 dB, a decomposition `d` (`db4`, 6 levels), a threshold `t` and the inverse transform `y`, which is the output. The nodes `x`, `n`, `d` and `y` must not be changed, only the threshold is edited. For now it works by the `visu` rule and gives a modest SNR gain.

Choose the threshold rule so that the SNR gain reaches 7.7 dB.

## Goals {#goals}

1. The SNR of `y` relative to `x` is higher than the SNR of `n` by at least 7.7 dB.
2. Every path from `n` to `y` goes through a `threshold` block.

## Hints {#hints}

1. The `threshold` block has four rules: `bayes`, `visu`, `sure`, `minimax`; see {#audio-denoising}. Run the graph and compare the SNR shown by goal 1.
2. `visu` picks one large threshold $\sigma\sqrt{2\ln N}$ and cuts the small peaks of the signal along with the noise. A rule that adapts the threshold to each level separately keeps more of the useful part.
3. Switching `mode` from `soft` to `hard` with the `visu` rule helps but not enough: check it. The `minimax` rule falls short too.

## Solution {#solution}

The `bayes` rule is needed: it computes the threshold $\lambda = \sigma^2/\sigma_x$ at each level, where $\sigma_x$ is the estimate of the standard deviation of the signal itself in that band. Where there is a lot of signal the threshold is small, where there is almost only noise it is large. The SNR at the input is {{m.in}} dB, at the output {{m.out}} dB. In the same conditions the `visu` rule with the soft threshold gives a gain of 3.6 to 4.5 dB, with the hard one 5.8 to 6.9 dB, `minimax` 5.6 to 6.8 dB (over 12 runs with different noise), so the 7.7 dB goal is reached only by the adaptive `bayes` threshold: 8.4 to 9.5 dB. Details in {#audio-denoising}.
