+++
id = "time_stretch"
title = "Time stretch and pitch shift"
theory = ["multirate-time-stretch", "stft"]
source = "src/core/tempo.cpp"
function = "vocoder"
test = "tests/core/test_tempo.cpp"
views = ["tempo"]
+++

## Purpose

Change the duration of a sound without changing its pitch, or the pitch without changing the duration — and hear how this differs from a plain speed change, which changes both.

## Formulas

STFT frames $X(m,k)$ with hop $R$ and a Hann window of length $N$. Output frame $j$ is taken from the input point $t_j = j\,r$, where $r$ is how many times faster; synthesis uses the same hop $R$, so the duration divides by $r$.

The magnitude is a linear interpolation of neighbouring frames, the phase an accumulated phase difference:

$$\lvert Y(j,k)\rvert = (1-\alpha)\lvert X(i,k)\rvert + \alpha\lvert X(i+1,k)\rvert, \qquad i = \lfloor t_j \rfloor, \quad \alpha = t_j - i$$

$$\varphi(j,k) = \varphi(j-1,k) + \arg X(i_{j-1}+1,k) - \arg X(i_{j-1},k)$$

Phase locking (Laroche–Dolson): bin $k$ in the region of peak $p$ follows the peak's phase with the offset it had in the nearest input frame $c = \lfloor t_j + 1/2 \rfloor$:

$$\varphi(j,k) = \varphi(j,p) + \arg X(c,k) - \arg X(c,p)$$

WSOLA: piece $j$ is taken with the shift $\delta \in [-R, R]$ from the point $jRr$ whose shape best continues the previous piece $s_{j-1}$:

$$\delta_j = \arg\max_{\delta} \frac{\sum_n s_{j-1}[n+R]\; x[jRr+\delta+n]}{\sqrt{\sum_n x[jRr+\delta+n]^2}}$$

A pitch shift by $n$ semitones: a stretch with $r = 2^{-n/12}$ and resampling by $r$, frequencies are multiplied by $2^{n/12}$. Speed, like a record: $y[n] = x(r\,n)$, frequencies are multiplied by $r$.

## Algorithm and cost

1. STFT frames are computed on demand: each output frame needs two neighbouring input frames, a few frames are kept in memory.
2. An output frame: interpolated magnitudes, accumulated phases (locked to peaks for «vocoder + phases»), inverse FFT, window, overlap-add; at the end, division by the sum of squared windows.
3. WSOLA: the correlation over $2R+1$ shifts via the FFT, energies by prefix sums.
4. Pitch shift: the stretch, then FFT resampling as `scipy.signal.resample`, cut to the input length.

The vocoder costs $O(M\,N\log N)$ for $M = \lceil F/r \rceil$ output frames from $F$ input ones; WSOLA $O\big(M\,(N+2R)\log(N+2R)\big)$.

## With this block's numbers

Mode {{block.mode}}, {{block.rate}} times faster, shift {{block.semitones}} st, method {{block.method}}; window $N$ = {{block.n_fft}}, hop $R$ = {{block.hop}}.

Duration {{metric.duration}}; pitch {{metric.f0_in}} → {{metric.f0_out}}, off the expected by {{metric.cents}} cents.

## Checks

- The vocoder and the pitch shift match `librosa.effects.time_stretch` and `pitch_shift(res_type="fft")` from librosa 1.0 to $10^{-12}$; the resampler matches `scipy.signal.resample`.
- At $r = 1$ all three methods return the input.
- A 220 Hz tone stays 220 Hz after a stretch by 0.75 and 1.5; a shift by ±12 semitones gives 440 and 110 Hz — within 1 cent.

## Links

- §3.8 — stretching without a pitch change.
- §11.9 — STFT and the COLA condition.
- §3.9 — the YIN pitch estimate.

## Implementation

A vocoder step: phase accumulation and locking to peaks.

```cpp
        if (j == 0) {
            acc = p0;
        } else {
            for (std::size_t k = 0; k < bins; ++k) acc[k] += diff[k];
            if (locked) lock_phases(acc, mag, t - static_cast<double>(i0) < 0.5 ? p0 : p1);
        }
        for (std::size_t k = 0; k < bins; ++k) diff[k] = p1[k] - p0[k];
```
