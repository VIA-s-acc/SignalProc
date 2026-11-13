# Butterworth order

A lowpass filter at $f_s = 8000$ Hz must meet a spec: at $f_p = 1000$ Hz the gain is not below $-3$ dB, at $f_{st} = 1500$ Hz the attenuation is at least 40 dB. Find the smallest order $N$ of a Butterworth filter that does this. The answer is an integer.

## Hints {#hints}

1. The Butterworth order: $N \ge \dfrac{\lg\bigl((10^{R_s/10}-1)/(10^{R_p/10}-1)\bigr)}{2\lg(\Omega_s/\Omega_p)}$ with $R_p = 3$ dB, $R_s = 40$ dB, and the digital frequencies mapped to analogue ones by pre-warping $\Omega = \tan(\pi f/f_s)$; see {#cls-iir-prototypes}.
2. The ratio $\Omega_s/\Omega_p = \tan(3\pi/16)/\tan(\pi/8)$ is about 1.613. A fractional $N$ from the formula is rounded up.
3. Check the answer on the Filters tab: a filter of this order with cutoff 1000 Hz must give at most $-40$ dB at 1500 Hz, and a filter one order lower must give more.

## Solution {#solution}

The answer is {{expected}}. The formula gives $\lg(10^4 - 1)/(2 \lg 1.613) \approx 9.64$, rounded up it is {{expected}}. Check with `iir` (Butterworth, cutoff 1000 Hz, $f_s = 8000$ Hz): order {{expected}} gives {{m.att_n}} dB at 1500 Hz, one order lower gives only {{m.att_m}} dB; at the cutoff both give about {{m.edge_n}} dB. Butterworth has no ripple, and its roll-off over a given transition band is the slowest among the classic families, which is why the order is large. Details in {#classic}.
