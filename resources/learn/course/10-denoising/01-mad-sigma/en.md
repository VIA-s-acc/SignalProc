# Noise level from the MAD

The graph builds a noisy signal `n` (a Donoho–Johnstone {{sig}} plus white noise of unknown level), decomposes it with the db4 wavelet and takes the finest detail $d_1$. The chain `abs → median → cut → stats` computes the median of $|d_1|$: the `median` node has a window as long as the whole detail and `cut` keeps its central sample, so in the report of node `st` the field `rms` is $\operatorname{median}|d_1|$.

Estimate the RMS value of the noise with $\hat\sigma = \operatorname{median}|d_1| / 0.6745$. The answer is a number, tolerance 5 %.

## Hints {#hints}

1. Run the graph with `r` and find the report of `st`: the field `rms` is the median of $|d_1|$, not yet divided by 0.6745.
2. For Gaussian noise $\operatorname{median}|N(0,\sigma^2)| = 0.6745\,\sigma$. So the median has to be divided by 0.6745, not multiplied.
3. The median is used rather than the mean or the maximum because almost all of $d_1$ is noise, and the few large coefficients from the signal hardly move the median; see {#audio-denoising}.

## Solution {#solution}

$\hat\sigma = \operatorname{median}|d_1| / 0.6745 = {{expected}}$. The true noise level in this graph, computed as the RMS of the difference `n − x`, is {{m.true}}: the estimate differs from it by a few percent. The median is robust to outliers, so the large signal coefficients in $d_1$ hardly move it, while the mean or the RMS of $d_1$ would overestimate. This $\hat\sigma$ enters the formulas of all the threshold rules; see {#audio-denoising}.
