+++
id = "reverb"
title = "Echo and reverberation"
theory = ["cls-allpass-notch", "lti-z-dtft", "freq-impulse-step"]
source = "src/pipeline/blocks_fx.cpp"
function = "reverb"
test = "tests/pipeline/test_blocks_fx.cpp"
views = ["signal"]
+++

## Purpose

Add delayed copies of the signal to itself: `echo` uses one delay line with feedback, repeating at equal intervals; `reverb` is the Freeverb network of eight comb filters with high-frequency damping and four diffusers, which gives a dense room "tail".

## Formulas

Echo: delay $D = \lfloor \tau f_s/1000 + 1/2 \rfloor \ge 1$ samples ($\tau$ in ms), feedback $g$, share $m$:

$$e[n] = x[n-D] + g\,e[n-D], \qquad E(z) = \frac{z^{-D}}{1 - g z^{-D}}, \qquad y = (1-m)\,x + m\,e$$

Impulse response: $1-m$ at zero and $m\,g^{j-1}$ at the points $jD$, $j \ge 1$. The magnitude of the wet path is a comb with peaks at multiples of $f_s/D$ and a peak-to-dip ratio $(1+\lvert g\rvert)/(1-\lvert g\rvert)$.

Reverberation (Freeverb, Jezar): the line lengths are given for 44.1 kHz and scaled, $L = \max\big(1, \lfloor L_{44} f_s/44100 + 1/2 \rfloor\big)$; combs $L_{44} \in \{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617\}$, diffusers $\{556, 441, 341, 225\}$.

Each comb is a line $b$ of length $L_i$ with a one-pole low-pass $f$ in the loop; room size $R$ and damping $D_a$ set

$$o[n] = b[n - L_i], \quad f[n] = (1-d)\,o[n] + d\,f[n-1], \quad b[n] = 0.015\,x[n] + g\,f[n], \qquad g = 0.28R + 0.7, \quad d = 0.4D_a$$

$$C_i(z) = \frac{0.015\,z^{-L_i}}{1 - g\,\frac{1-d}{1 - d z^{-1}}\,z^{-L_i}}, \qquad s = \sum_{i=1}^{8} o_i$$

The loop gain is $g$ at zero frequency and $g(1-d)/(1+d)$ at Nyquist: high frequencies die out faster. The 60 dB decay at low frequencies is $T_{60} \approx -3L_i/(f_s \log_{10} g)$; for $R = 0.5$ ($g = 0.84$) and $L = 1617$ it is about 1.5 s.

Four diffusers in series, feedback $1/2$:

$$v[n] = s[n] + \tfrac12 v[n-L], \qquad s'[n] = v[n-L] - s[n], \qquad P(z) = \frac{-1 + \tfrac32 z^{-L}}{1 - \tfrac12 z^{-L}}$$

This is not the Schroeder all-pass $(-g + z^{-L})/(1 - g z^{-L})$ of §9.8: $\lvert P\rvert$ swings between $1$ and $5/3$. The original Freeverb is built this way and the code follows it. The output is $y = W_d\,x + 3W_w\,s'$; unlike the original, the dry signal is not multiplied by 2, and there is no stereo spread (+23 samples on the right channel) — every channel passes the same lines on its own.

## Algorithm and cost

1. Echo: the delay is rounded from milliseconds, $D = 0$ is an error; a recursion over one line.
2. Reverberation: the line lengths are rescaled for $f_s$; if the longest comb exceeds $2^{20}$ samples, the block refuses.
3. The eight combs run over the whole signal one after another, each with its own circular buffer and loop low-pass; their outputs are summed.
4. The sum passes the four diffusers in series and is then mixed with the dry signal.

Echo costs $O(N)$, reverberation $O(12N)$ per channel. Memory: the output and one line at a time (up to 1617 samples at 44.1 kHz).

## With this block's numbers

Echo: delay {{block.delay_ms}} ms, feedback {{block.feedback}}, wet share {{block.mix}}.

Reverberation: room size {{block.room_size}}, high-frequency damping {{block.damping}}, wet {{block.wet}} (multiplied by 3 on top), dry {{block.dry}}.

Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- `reverb` matches the line-by-line Freeverb in Python from `tools/gen_golden.py` to $10^{-12}$ for (size, damping, wet, dry) = (0.5, 0.5, 0.33, 0.7) and (0.95, 0.1, 1, 0).
- With wet = 0, dry = 1 the output equals the input exactly; the two stereo channels match separate mono runs exactly.
- `echo` (2 ms, $g = 0.5$, $m = 0.4$) matches `scipy.signal.lfilter` with numerator $z^{-D}$ and denominator $1 - 0.5z^{-D}$ to $10^{-10}$; without a sample rate the block refuses (`tests/pipeline/test_blocks_more2.cpp`).

## Links

- §9.8 — all-pass filters: the true Schroeder diffuser and how the Freeverb section differs from it.
- §1.2 — the $z$-transform: the delay $z^{-D}$ and the recursion in the denominator.
- §8.5 — impulse response: a sparse comb of pulses for the echo, a dense tail for the reverb.
- The "Modulation effects" card — a delay that changes over time; the `comb` block — a comb filter without mixing.

## Implementation

One Freeverb comb: read the line, low-pass in the loop, write the input plus feedback.

```cpp
        for (std::size_t k = 0; k < x.size(); ++k) {
            const double out = line[at];
            store = out * (1 - damp) + store * damp;
            line[at] = 0.015 * x[k] + store * feedback;
            at = at + 1 == line.size() ? 0 : at + 1;
            acc[k] += out;
        }
```
