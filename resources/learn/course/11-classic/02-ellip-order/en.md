# Elliptic filter order

The spec is the same as in 11.1: $f_s = 8000$ Hz, at $f_p = 1000$ Hz the gain is not below $-3$ dB, at $f_{st} = 1500$ Hz the attenuation is at least 40 dB. Now the filter is elliptic: passband ripple $R_p = 3$ dB, stopband ripple $R_s = 40$ dB. Find the smallest order $N$. The answer is an integer.

## Hints {#hints}

1. The elliptic order is computed with complete elliptic integrals: $N \ge \dfrac{K(k)\,K'(k_1)}{K'(k)\,K(k_1)}$, where $k = \Omega_p/\Omega_s$ is the ratio of the pre-warped frequencies, $k_1 = \sqrt{(10^{R_p/10}-1)/(10^{R_s/10}-1)}$, $K'(k) = K(\sqrt{1-k^2})$; see {#cls-iir-prototypes}.
2. Here $k \approx 0.620$ and $k_1 \approx 0.00998$. A fractional $N$ is rounded up.
3. Check the answer on the Filters tab: family `ellip`, cutoff 1000 Hz, `rp` = 3, `rs` = 40; at 1500 Hz the level must be at most $-40$ dB, and with one order lower it must be higher.

## Solution {#solution}

The answer is {{expected}}. The formula gives about 3.43, rounded up it is {{expected}}. Check with `iir` (`ellip`, cutoff 1000 Hz, `rp` = 3, `rs` = 40): order {{expected}} gives {{m.att_n}} dB at 1500 Hz, one order lower gives only {{m.att_m}} dB. For the same requirements Butterworth needs order 10 and the elliptic filter {{expected}}: ripple in both bands buys the steepest roll-off at a given order. Details in {#classic}.
