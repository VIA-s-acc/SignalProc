+++
id = "pitch"
title = "Fundamental frequency: YIN"
theory = ["pitch-yin"]
source = "src/core/pitch.cpp"
function = "yin_frame"
test = "tests/pipeline/test_blocks_analysis.cpp"
views = ["tempo"]
+++

## Purpose

Find the fundamental frequency $f_0$ in every frame — of a voice, an instrument or a tone after a pitch shift — and tell voiced frames from noise and silence.

## Formulas

The difference function of a frame with comparison window $W$, through energies and the correlation:

$$d(\tau) = \sum_{j=0}^{W-1} \big(x[j] - x[j+\tau]\big)^2 = e_0 + e_\tau - 2\,r(\tau)$$

The cumulative mean normalised difference:

$$d'(\tau) = \frac{\tau\, d(\tau)}{\sum_{s=1}^{\tau} d(s)}, \qquad d'(0) = 1$$

The period is the first $\tau \ge \tau_{\min}$ with $d'(\tau) < \theta$, moved to the local minimum and refined by a parabola:

$$\hat\tau = \tau + \frac{d'(\tau-1) - d'(\tau+1)}{2\big(d'(\tau-1) - 2\,d'(\tau) + d'(\tau+1)\big)}, \qquad f_0 = \frac{f_s}{\hat\tau}$$

Bounds: $\tau_{\min} = \lfloor f_s/f_{\max} \rfloor$, $\tau_{\max} = \lceil f_s/f_{\min} \rceil$, $W = N/2$.

## Algorithm and cost

1. Frames of length $N$ with hop $R$; the comparison window is the first half of the frame.
2. The correlation $r(\tau)$ via the FFT, the energies $e_\tau$ by a running sum.
3. Threshold $\theta$, descent to the minimum, the parabola; if $d'$ never drops below the threshold the frame is unvoiced and gives 0.
4. The block's pitch is the median over voiced frames.

Cost $O(M\,N\log N)$ for $M$ frames instead of $O(M\,W\,\tau_{\max})$ for the direct sum.

## With this block's numbers

Pitch before processing {{metric.f0_in}}, after {{metric.f0_out}}.

## Checks

- The `pitch` block matches the numpy YIN reference frame by frame: a tone with harmonics, noise and trailing silence.
- A 220 Hz tone shifted by ±12 semitones gives 440 and 110 Hz by YIN within 1 cent.

## Links

- §3.9 — YIN.
- §3.8 — the stretch and pitch shift this pitch checks.

## Implementation

Picking the period: the first dip below the threshold, then descent to the minimum.

```cpp
    auto tau = s.tau_min;
    while (tau <= top && !(cmnd[tau] < threshold)) ++tau;
    if (tau > top) return 0.0;
    while (tau + 1 <= top && cmnd[tau + 1] < cmnd[tau]) ++tau;
```
