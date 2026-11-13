# Soft threshold

The graph holds a clean signal `x` (Donoho–Johnstone Doppler) and a node `n` that adds white noise at SNR {{snr}} dB to it. The output is node `y`; for now it simply repeats `n`. Build a denoiser as the chain "DWT → threshold → inverse DWT" starting from the noisy `n`: the clean `x` must not enter the chain, it is only the reference for measuring. The ready-made `denoise` block is forbidden. Do not change the nodes `x` and `n`.

## Goals {#goals}

1. The SNR of `y` relative to `x` is higher than the SNR of `n` by at least 6 dB.
2. Every path from `n` to `y` goes through a `threshold` block.
3. The `denoise` block is not used.
4. The clean signal `x` reaches the output only through the noisy recording `n`: it cannot be mixed in around it.

## Hints {#hints}

1. Noise is spread almost evenly over all detail coefficients, while a smooth signal has few large ones. So the noise is removed in the wavelet domain: `dwt`, then shrinkage of the small coefficients, then `idwt`; see {#audio-denoising}.
2. The `threshold` block takes coefficients and returns coefficients: put it between `dwt` and `idwt`. Wavelet `sym8` with 5 levels and the soft threshold by the `bayes` rule will do.
3. To see the SNR, put probes on `n` and `y` and compare them with `x`, or check the goals with `F2`.

## Solution {#solution}

The graph: `n → dwt(sym8, 5 levels) → threshold(bayes, soft) → idwt → y`. The SNR at the input is {{m.in}} dB, at the output {{m.out}} dB, a gain of about 10 dB; the 6 dB goal leaves a margin. The soft threshold zeroes the coefficients whose magnitude is below $\lambda$ and pulls the rest towards zero by $\lambda$. Almost all the noise goes while the Doppler oscillations stay in a few large coefficients; $\sigma$ is estimated automatically from the detail $d_1$, where there is little signal (see {#audio-denoising}).
