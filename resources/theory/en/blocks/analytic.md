+++
id = "analytic"
title = "Analytic signal"
theory = ["toeplitz-circulant", "cls-fir-types", "synchrosqueezing"]
source = "src/core/analysis.cpp"
function = "analytic_signal"
test = "tests/core/test_analysis.cpp"
views = ["under_the_hood"]
+++

## Purpose

Add an imaginary part to the signal so that only positive frequencies remain, and read the amplitude and phase at every instant from it: `hilbert` gives the Hilbert transform or the phase, `envelope` the envelope, `inst_freq` the instantaneous frequency in Hz.

## Formulas

The analytic signal via a DFT of length $N$, as `scipy.signal.hilbert`:

$$z[n] = \frac{1}{N}\sum_{k=0}^{N-1} g[k]\,X[k]\,e^{2\pi i k n/N}, \qquad g[k] = \begin{cases} 1, & k = 0 \text{ or } 2k = N \\ 2, & 0 < 2k < N \\ 0, & 2k > N \end{cases}$$

$\mathrm{Re}\,z = x$, $\mathrm{Im}\,z$ is the Hilbert transform. It is circular: the signal is treated as one period, so distortion appears near the edges where the start does not continue the end.

`hilbert`: `imag` is $\mathrm{Im}\,z[n]$, `phase` is $\varphi[n] = \arg z[n] \in (-\pi, \pi]$, `unwrapped` is the phase with $2\pi k$ added at jumps of at least $\pi$, as `numpy.unwrap`.

`envelope`: $a[n] = \lvert z[n]\rvert$.

`inst_freq` is the forward difference of the unwrapped phase, so the output is one sample shorter than the input:

$$f[n] = \frac{f_s}{2\pi}\big(\varphi[n+1] - \varphi[n]\big), \qquad n = 0, \dots, N-2$$

If $\lvert z[n]\rvert$ or $\lvert z[n+1]\rvert$ is at most $10^{-12}\max\lvert z\rvert$, the phase is undefined and the previous value is repeated (0 at the start).

## Algorithm and cost

1. A complex FFT of length $N$ (any length, not only powers of two), multiplication by $g[k]$, the inverse FFT with division by $N$.
2. `hilbert` takes the imaginary part or the argument and, for `unwrapped`, unwraps the phase.
3. `envelope` takes the modulus; `inst_freq` the argument, the unwrapping and the difference with the factor $f_s/2\pi$, skipping quiet samples.

Cost $O(N\log N)$ per channel; memory — $N$ complex samples.

## With this block's numbers

`hilbert`: output {{block.output}}.

Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- The real and imaginary parts of `analytic_signal` match `scipy.signal.hilbert` to $10^{-12}$ for lengths 37, 64 and 1.
- The `hilbert` block in all three modes matches `scipy.signal.hilbert`, `numpy.angle` and `numpy.unwrap` to $10^{-10}$; one sample has imaginary part 0.
- A 1 kHz tone of amplitude 0.5 at $f_s = 48000$: away from the edges (480 samples) the envelope is 0.5 and the instantaneous frequency 1000 Hz, both to $10^{-3}$; with a gap of zeros the frequency stays finite.
- In the pipeline `inst_freq` of 800 samples gives 799, and in the middle the 1000 Hz tone reads to $10^{-6}$ Hz; `envelope` matches the core and keeps $f_s$.

## Links

- §2.3 — circulants and DFT diagonalisation: the multiplication by $g[k]$ is a circular filter.
- §9.2 — linear-phase FIR filters, the Hilbert one among them (the `remez` block with `type = hilbert`).
- §11.7 — synchrosqueezing: the same instantaneous frequency, but from the CWT.

## Implementation

Positive frequencies doubled and negative ones zeroed between the two FFTs.

```cpp
    a = fft(std::move(a), true);
    for (std::size_t k = 1; k < n; ++k) {
        if (2 * k < n) a[k] *= 2.0;
        else if (2 * k > n) a[k] = 0.0;
    }
    return fft(std::move(a), false);
```
