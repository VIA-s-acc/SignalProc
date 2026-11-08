+++
id = "modulation"
title = "Modulation effects"
theory = ["cls-allpass-notch", "lti-frequency-response", "freq-group-delay"]
source = "src/pipeline/blocks_fx.cpp"
function = "phaser"
test = "tests/pipeline/test_blocks_fx.cpp"
views = ["signal"]
+++

## Purpose

Vary one property of a sound over time, driven by a slow oscillator (LFO) or by the input itself: `tremolo` the loudness, `modulated_delay` the delay (chorus, flanger, vibrato), `phaser` the phase of an all-pass chain, `am` and `fm` the amplitude and the frequency of a carrier that the input modulates.

## Formulas

The LFO phase at rate $f_r$ is computed from the sample index: $\phi_k = 2\pi f_r k/f_s$, $\tau = \phi_k \bmod 2\pi$. Shapes: sine $L = \sin\phi_k$; square $L = 1$ for $\tau < \pi$, else $-1$; triangle $L = 2\tau/\pi - 1$ for $\tau < \pi$, else $3 - 2\tau/\pi$ (it starts at $-1$, like `scipy.signal.sawtooth(φ, 0.5)`).

Tremolo with depth $D$ — the gain moves between $1 - D$ and $1$:

$$y_k = x_k\big(1 - \tfrac12 D\,(1 - L(\phi_k))\big)$$

Modulated delay (the LFO is always a sine) from $B$ to $B + \Delta$ ms, linear interpolation along the line $v$, with $v[i] = 0$ for $i < 0$:

$$d_k = \max\big(1, (B + \Delta\,\tfrac{1 + \sin\phi_k}{2})\,f_s/1000\big), \qquad i = \lfloor k - d_k \rfloor, \quad \alpha = k - d_k - i$$

$$w_k = (1-\alpha)\,v[i] + \alpha\,v[i+1], \qquad v_k = x_k + g\,w_k, \qquad y_k = (1-m)\,x_k + m\,w_k$$

Chorus: $g = 0$; flanger: $g$ = `feedback`; vibrato: $g = 0$ and $m = 1$ (the `mix` parameter has no effect).

Phaser: the break frequency sweeps geometrically from $f_{lo}$ to $f_{hi}$ and back, and the coefficient is recomputed at every sample:

$$f_c(k) = f_{lo}\big(f_{hi}/f_{lo}\big)^{(1 - \cos\phi_k)/2}, \qquad a_k = \frac{t - 1}{t + 1}, \quad t = \tan\frac{\pi f_c(k)}{f_s}$$

$S$ identical sections $A(z) = \dfrac{a + z^{-1}}{1 + a z^{-1}}$, $\lvert A\rvert = 1$, phase $-\pi/2$ at $f_c$ (§9.8 uses the opposite sign of the coefficient: $a_{\text{book}} = -a$). The chain input is $u_k = x_k + g\,s_{k-1}$, where $s$ is the output of the last section, and the output is $y_k = (1-m)\,x_k + m\,s_k$. With $m = 1/2$ and $g = 0$ the notches sit where the chain phase is an odd multiple of $\pi$: $\lfloor S/2 \rfloor$ notches inside the band.

AM and FM — the input $x$ modulates a carrier $f_c$ with deviation $\Delta f$:

$$y_k^{\mathrm{AM}} = (1 + D\,x_k)\cos\frac{2\pi f_c k}{f_s}, \qquad y_k^{\mathrm{FM}} = \cos\Big(\frac{2\pi f_c k}{f_s} + \frac{2\pi\,\Delta f}{f_s}\sum_{j=0}^{k-1} x_j\Big)$$

The AM spectrum is the carrier line plus copies $\tfrac{D}{2}X$ at $\pm f_c$; with $D\max\lvert x\rvert > 1$ the envelope changes sign (overmodulation). In FM the sum runs to $k-1$: the current sample enters the phase of the next one; the instantaneous frequency is $f_c + \Delta f\,x_k$, for a tone $f_m$ the index is $\beta = \Delta f/f_m$, Carson's bandwidth $\approx 2(\Delta f + f_m)$.

## Algorithm and cost

1. The LFO and carrier phases are recomputed from $k$, not accumulated, so they do not drift.
2. Tremolo and AM cost one multiplication per sample; FM keeps a running sum of the input.
3. The modulated delay writes into a line as long as the whole signal and reads it between two neighbouring samples.
4. The phaser takes one $\tan$ per sample, then runs $S$ direct-form sections, keeping their state.
5. Channels are processed independently; without a sample rate the blocks refuse, the phaser also when $f_{lo}$ or $f_{hi}$ is not below Nyquist.

Cost $O(N)$ per channel for tremolo, delay, AM and FM, and $O(SN)$ for the phaser; memory $O(N)$ (the output and the delay line).

## With this block's numbers

Tremolo: LFO {{block.rate_hz}} Hz, shape {{block.shape}}, depth {{block.depth}} — the gain dips to one minus the depth.

Delay: mode {{block.mode}}, base {{block.base_ms}} ms plus up to {{block.depth_ms}} ms, feedback {{block.feedback}}, effect share {{block.mix}}.

Phaser: {{block.stages}} sections, break from {{block.min_hz}} to {{block.max_hz}} Hz. AM and FM: carrier {{block.carrier_hz}} Hz, FM deviation {{block.deviation_hz}} Hz.

Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- `tremolo` (three shapes, 7 Hz, depth 0.8), `modulated_delay` (chorus, two flangers, one with feedback $-0.8$ and base 0, vibrato) and `phaser` (4 sections 200–2000 Hz; 12 sections 3000→100 Hz with feedback $-0.7$) match the line-by-line Python references in `tools/gen_golden.py` to $10^{-12}$.
- `am` matches `(1 + 0.6x)·cos` in numpy to $10^{-12}$, `fm` the formula with a `cumsum` that excludes the current sample to $10^{-10}$; with a zero input `fm` gives a pure carrier to $10^{-15}$.
- Vibrato does not depend on `mix`; `mix` = 0 for the delay and depth 0 for the tremolo return the input exactly; the phaser rejects `max_hz` = 4000 at $f_s$ = 8000; without a sample rate `tremolo` and `fm` refuse.

## Links

- §9.8 — first-order all-pass sections, from which the phaser is built.
- §1.3 — magnitude and phase response: the phaser changes only the phase, the notches come from mixing with the dry signal.
- §8.4 — group and phase delay: the flanger is a comb filter with a drifting delay $d_k$.
- The "Echo and reverberation" card — fixed delays with feedback.

## Implementation

One phaser step: a new coefficient from the sweeping break frequency and a pass through the all-pass chain.

```cpp
        const double fc = lo * std::pow(hi / lo, (1 - std::cos(phase(rate, k, fs))) / 2);
        const double t = std::tan(kPi * fc / fs);
        const double a = (t - 1) / (t + 1);
        double u = x[k] + feedback * last;
        for (std::size_t s = 0; s < stages; ++s) {
            const double v = a * (u - ys[s]) + xs[s];
            xs[s] = u;
            ys[s] = v;
            u = v;
        }
```
