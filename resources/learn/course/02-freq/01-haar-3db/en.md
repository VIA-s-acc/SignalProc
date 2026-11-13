# The −3 dB point of the Haar filter

For the Haar lowpass $h_0$ (`haar.h0` in the program) find the frequency where the magnitude response is 3 dB below its value at zero frequency. The answer is the dimensionless number $\omega/\pi$ between 0 and 1.

## Hints {#hints}

1. The magnitude response is $|H_0(e^{j\omega})|$, see {#freq-magnitude}. For the two taps $h_0 = \tfrac{1}{\sqrt2}[1,\ 1]$ the sum $1 + e^{-j\omega}$ factors into $e^{-j\omega/2}$ and a cosine.
2. At zero frequency the gain is $\sqrt2$. A 3 dB drop in power is a factor $1/\sqrt2$ in amplitude.
3. Check on the «Filters» tab: choose `haar`, filter $h_0$, and find the point on the response curve that is 3 dB below the peak.

## Solution {#solution}

$|H_0(e^{j\omega})| = \sqrt2\,\cos(\omega/2)$ on $[0, \pi]$. The level $\tfrac{1}{\sqrt2}$ of the peak is reached when $\cos(\omega/2) = \tfrac{1}{\sqrt2}$, that is at $\omega = \pi/2$. The answer is $\omega/\pi = {{expected}}$.

Check on a graph: a tone at $\omega = \pi/2$ passes through `haar.h0` with gain {{m.g}} dB, while the peak at zero frequency is $20\log_{10}\sqrt2 \approx 3.01$ dB, exactly 3 dB higher. The Haar filter is a very gentle lowpass; for bands see {#freq-band-metrics}.
