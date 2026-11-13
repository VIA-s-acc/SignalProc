# SNR from the report

The start graph holds a clean tone `x` of amplitude {{s}} and white noise `w` with RMS value {{a}}; node `n` adds them. The block `chk` (`check`, criterion `snr`) compares the noisy signal with the clean one and computes the signal-to-noise ratio in dB.

Run the graph and read the SNR from the report of node `chk`, field `snr_db`. The answer is a number in dB, tolerance 1 %.

## Hints {#hints}

1. Run the graph with `r` and find the report of `chk` in the reports panel under the diagram.
2. The report has several fields: `max_abs`, `rms` and `snr_db`. You need the last one.
3. You can estimate the answer before running: SNR $= 10\log_{10}(P_x / P_e)$, the power of a sine of amplitude $A$ is $A^2/2$, and the power of white noise is the square of its RMS value $\sigma$. The report gives a close number: one realisation of the noise has a slightly different power.

## Solution {#solution}

The SNR is {{expected}} dB. The `check` block takes the clean signal as the reference, counts the difference $n - x$, that is the noise itself, as the error and prints $10\log_{10}(P_x/P_e)$. The estimate $10\log_{10}\bigl(A^2 / (2\sigma^2)\bigr)$ with $A = {{s}}$ and $\sigma = {{a}}$ differs from the report by a fraction of a decibel: the power of a short noise realisation is not exactly $\sigma^2$. Definitions are in {#conventions}.
