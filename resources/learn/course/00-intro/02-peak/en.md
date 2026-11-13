# Signal peak

The start graph has a generator `x` producing a sine of 1000 Hz with amplitude {{a}} at a sampling rate of 8000 Hz. A `stats` block is attached to it and computes RMS, peak, mean and other numbers.

Run the graph and find the peak $\max_n |x[n]|$ of signal `x`. The answer is a number.

## Hints {#hints}

1. Run the graph with `r` and open the `stats` probe: the block report has a field with the peak.
2. The peak is the largest absolute sample, so the sign does not matter. For a pure sine it equals the amplitude if the samples land on a crest.
3. How many samples fit in one period? Do the samples pass through a crest of the sine (phase $\pi/2$)?

## Solution {#solution}

The peak is {{expected}}. The tone $A\sin(2\pi f n / f_s)$ with $f/f_s = 1/8$ repeats every 8 samples, and among them $n = 2$ has $\sin(\pi/2) = 1$, so that sample equals $A$. With a frequency that does not divide the sampling rate so neatly, the sampled peak would be a little below the amplitude. Signal notation is in {#conv-signals}.
