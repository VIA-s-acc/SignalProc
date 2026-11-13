# Hard or soft threshold

The graph holds a noisy Donoho–Johnstone Bumps signal (sharp peaks on a zero background) at SNR {{snr}} dB, a `sym8` decomposition with 5 levels, a threshold by the `visu` rule and the inverse transform; the report `chk` shows the SNR of the result against the clean `x`. For now the threshold is soft (`mode=soft`).

Which threshold, `hard` or `soft`, gives the higher SNR on this signal with the `visu` rule? Choose the answer.

## Hints {#hints}

1. Run the graph with `r` and write down `snr_db` from the report `chk`. Then change `mode` of node `t` to `hard`, run again and compare.
2. The soft threshold not only zeroes the small coefficients but also pulls all the remaining ones towards zero by $\lambda$: this bias cuts the height of narrow peaks. The hard threshold leaves the large coefficients as they are; see {#audio-denoising}.
3. The `visu` rule gives a large threshold $\lambda = \sigma\sqrt{2\ln N}$, and the bias of the soft threshold is especially visible with it.

## Solution {#solution}

The hard threshold has the higher SNR: {{m.sh}} dB against {{m.ss}} dB for the soft one. The Bumps peaks are narrow and their energy sits in a few large coefficients. The hard threshold keeps their values, the soft one reduces each by $\lambda$, which with the `visu` rule is a noticeable share, and loses peak height. The answer depends on the rule too: with `bayes` the threshold $\lambda$ is lower and on the same Bumps the soft threshold wins; check by changing `rule`. Details in {#audio-denoising}.
