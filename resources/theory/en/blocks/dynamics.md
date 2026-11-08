+++
id = "dynamics"
title = "Dynamics: compressor, limiter, gate"
theory = ["audio-band-energy", "freq-magnitude"]
source = "src/pipeline/blocks_effects.cpp"
function = "static_curve"
test = "tests/pipeline/test_blocks_more2.cpp"
views = ["signal"]
+++

## Purpose

Change the gain over time from the level of the signal itself: `compressor` and `limiter` turn down the loud parts above a threshold, `gate` turns down the quiet parts below it.

## Formulas

The level in dB is one for all $C$ channels (a linked detector). Peak and RMS:

$$L_{\mathrm{peak}}[n] = 20\log_{10}\max\big(\max_c \lvert x_c[n]\rvert, 10^{-10}\big)$$

$$P[n] = c_r P[n-1] + (1 - c_r)\,\frac{1}{C}\sum_c x_c[n]^2, \qquad L_{\mathrm{rms}}[n] = 10\log_{10}\max\big(P[n], 10^{-20}\big)$$

The smoothing coefficient for a time constant $\tau$ ms is $c(\tau) = e^{-1000/(\tau f_s)}$, $c(0) = 0$; within $\tau$ a step covers $1 - 1/e \approx 63$ %.

The static curve with threshold $T$, slope $S = 1/R$ and a quadratic knee of width $W$:

$$G(L) = \begin{cases} L, & L - T < -W/2 \\ L + (S - 1)\,\frac{(L - T + W/2)^2}{2W}, & \lvert L - T\rvert \le W/2 \\ T + (L - T)\,S, & L - T > W/2 \end{cases}$$

What is smoothed is not the level but the required reduction in dB, $\gamma^*[n] = G(L[n]) - L[n] \le 0$, with an attack/release branch; the initial $\gamma = 0$:

$$\gamma[n] = c\,\gamma[n-1] + (1 - c)\,\gamma^*[n], \qquad c = c_{\mathrm{att}} \text{ if } \gamma^*[n] < \gamma[n-1], \text{ else } c_{\mathrm{rel}}$$

$$y_c[n] = x_c[n]\cdot 10^{(\gamma[n] + M)/20}$$

The limiter is the same computation with $S = 0$, $W = 0$, $M = 0$ and the peak detector: $\gamma^* = \min(0, T - L)$. The gate: $\gamma^* = -\rho$ when $L < T$, else $0$, with no makeup $M$; for the gate the attack is the opening ($c_{\mathrm{att}}$ when $\gamma^* > \gamma[n-1]$) and the release is the closing.

## Algorithm and cost

1. Detector: the instantaneous peak or a one-pole smoothing of the power averaged over channels; a channel shorter than the others is padded with zeros.
2. The required reduction from the static curve (a step for the gate).
3. One-pole smoothing of the reduction in dB, with different coefficients for rise and fall.
4. Each channel is multiplied by $10^{(\gamma + M)/20}$ and keeps its own length.

There is no look-ahead: with zero attack the limiter cuts a peak in the very same sample, with a non-zero attack it lets overshoots through. Cost $O(NC)$; memory — the level and the gain, $N$ values each, plus the output.

## With this block's numbers

Detector {{block.detector}} (RMS window {{block.rms_ms}} ms), threshold {{block.threshold_db}} dB, ratio {{block.ratio}}:1, knee {{block.knee_db}} dB, makeup {{block.makeup_db}} dB.

Attack {{block.attack_ms}} ms, release {{block.release_ms}} ms; for the gate the closed depth is {{block.range_db}} dB.

Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- The compressor (peak: −12 dB, 4:1, no knee, 2/30 ms; RMS: −18 dB, 3:1, 6 dB knee, 0.5/10 ms, 4 dB makeup, 1.5 ms window), the limiter (−6 dB, attack 0 and 1 ms) and the gate (−6 dB, 40 dB range, 0.5/3 ms) on a stereo pair of channels of different lengths match the reference gain computer in Python to $10^{-10}$ in every channel.
- The limiter with zero attack keeps $\lvert y\rvert \le 10^{-6/20}(1 + 10^{-12})$.
- A quiet signal below the threshold passes the compressor unchanged; silence through the gate stays exactly zero.

## Links

- §12.5 — energy per frame: the RMS detector is a running estimate of the same energy.
- §8.2 — decibels and $20\lg$ for amplitude.
- The "Gain in dB" card — a constant factor; here every sample has its own factor.

## Implementation

The gain computer: the reduction from the static curve, smoothed with different attack and release.

```cpp
    Samples gain(level.size());
    double state = 0;
    for (std::size_t n = 0; n < level.size(); ++n) {
        const double target = static_curve(level[n], threshold, slope, knee) - level[n];
        const double coef = target < state ? attack : release;
        state = coef * state + (1 - coef) * target;
        gain[n] = state + makeup;
    }
    return apply_gain(a, gain);
```
