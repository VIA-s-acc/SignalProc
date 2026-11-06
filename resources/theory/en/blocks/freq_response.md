+++
id = "freq_response"
title = "Filter frequency response"
theory = ["freq-magnitude", "freq-group-delay", "freq-band-metrics", "lti-frequency-response"]
source = "src/core/freq.cpp"
function = "frequency_response"
test = "tests/core/test_freq.cpp"
views = ["filters"]
+++

## Purpose

Show how the filter $h$ passes sinusoids of different frequencies: the magnitude response (linear and in dB), the phase, the group delay and the band figures (the $-3$ dB cutoff, ripple, attenuation, transition band).

## Formulas

$$H(e^{i\omega}) = \sum_n h[n]\, e^{-i\omega n}, \qquad \lvert H(e^{i\omega}) \rvert, \qquad 20 \log_{10} \lvert H(e^{i\omega}) \rvert \ \text{dB}$$

$$\lvert H(1) \rvert = \Big\lvert \sum_n h[n] \Big\rvert, \qquad \lvert H(-1) \rvert = \Big\lvert \sum_n (-1)^n h[n] \Big\rvert$$

Here $H(1)$ is the value at $\omega = 0$, and $H(-1)$ is the value at $\omega = \pi$.

Group delay without phase unwrapping:

$$\tau_g(\omega) = \operatorname{Re} \frac{H_n(e^{i\omega})}{H(e^{i\omega})}, \qquad H_n(e^{i\omega}) = \sum_n n\, h[n]\, e^{-i\omega n}$$

Band metrics relative to the maximum of the magnitude response: $\omega_c$ is the first crossing of the level $-10 \log_{10} 2 \approx -3.01$ dB; ripple is the peak-to-peak spread of the dB magnitude in the passband; attenuation $A_s = -\max_{\text{stop}} \lvert H \rvert_{\text{dB}}$; transition band $\Delta\omega = \omega_{-20} - \omega_{-1}$, the distance between the crossings of the $-1$ and $-20$ dB levels.

## Algorithm and cost

1. A uniform grid $\omega_k = \pi k / K$, $k = 0, \dots, K-1$, on $[0, \pi)$.
2. At each point one pass over the taps accumulates both $H$ and $H_n$: a sum over the taps that accounts for the Laurent offset, without the FFT.
3. $\lvert H \rvert$, the dB magnitude with a floor of $-300$ dB, $\arg H$ and the unwrapped phase; $\tau_g = \operatorname{Re}(H_n / H)$ where $\lvert H \rvert > 10^{-12} \max \lvert H \rvert$, otherwise undefined.
4. Band metrics: level crossings are found by linear interpolation between neighboring grid points.

Cost $O(K L)$ complex terms; memory $O(K)$ per curve. For a lowpass filter the passband extends up to half the nominal band edge, and the stopband starts at one and a half times that edge (no higher than $\pi$); for highpass and bandpass filters ripple and attenuation are not defined.

## With this block's numbers

Filter {{block.filter}}: $L = {{block.length}}$ taps, a grid of $K = {{block.points}}$ points, i.e. $K \cdot L = {{block.points}} \cdot {{block.length}}$ terms. Linear phase — {{block.linear_phase}}. Multiplicity of the zero at $z = -1$ — {{block.zeros_at_minus_one}}: near $\pi$ the magnitude response behaves like $(\pi - \omega)^N$.

Cutoff $\omega_c = {{metric.cutoff_3db}}$; passband ripple {{metric.ripple_db}}; stopband attenuation {{metric.attenuation_db}}; transition band $\Delta\omega = {{metric.transition}}$.

## Checks

- Agreement with the reference `scipy.signal.freqz` and `group_delay`; $\lvert H(1) \rvert = \lvert \sum h \rvert$ and $\lvert H(-1) \rvert = \lvert \sum (-1)^n h \rvert$ (§8.2).
- Any orthogonal lowpass filter has its $-3$ dB cutoff exactly at $0.5\pi$ (§8.7): compare with $\omega_c = {{metric.cutoff_3db}}$.
- A wavelet lowpass filter has $\lvert H(1) \rvert = \sqrt2$, i.e. $+3.01$ dB above unity.
- A symmetric filter of length $L$ has $\tau_g \equiv (L-1)/2$ away from the zeros of the magnitude response; linear phase here: {{block.linear_phase}}.

## Links

- §1.4 — linear phase, types I–IV and forced zeros.
- §8.3 — wrapped and unwrapped phase, the pitfalls of jumps by $\pi$.
- §8.4 — group delay and phase delay.
- §8.6 — the zero map: show the known multiplicity at $-1$, not the computed roots.
- §8.7 — band metrics and power symmetry.

## Implementation

A grid point: the sum $H$ and the weighted sum $H_n$ in one pass over the taps.

```cpp
    for (std::size_t k = 0; k < points; ++k) {
        const double w = std::numbers::pi * static_cast<double>(k) / static_cast<double>(points);
        r.omega[k] = w;
        std::complex<double> sum{0, 0}, moment{0, 0};
        for (std::size_t j = 0; j < h.coeffs.size(); ++j) {
            const auto n = static_cast<double>(h.offset + static_cast<std::ptrdiff_t>(j));
            const auto term = h.coeffs[j] * std::polar(1.0, -w * n);
            sum += term;
            moment += n * term;
        }
        r.h[k] = sum;
        weighted[k] = moment;
    }
```
