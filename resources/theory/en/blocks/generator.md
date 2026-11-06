+++
id = "generator"
title = "Signal generator"
theory = ["conv-signals", "lti-z-dtft", "multirate-down-up"]
source = "src/io/generators.cpp"
function = "sweep"
test = "tests/io/test_generators.cpp"
views = ["signal"]
+++

## Purpose

Create a test signal of known shape: a sine, a sweep, an impulse, a step, noise of three colors, a set of harmonics, AM and FM signals, or one of the Donoho–Johnstone test signals. A known shape lets you check filters and wavelets against a response that can be predicted in advance.

## Formulas

With $t_n = n / f_s$ and $\omega = 2\pi f / f_s$ rad/sample, the sine and the harmonics:

$$x[n] = A \sin\left(\frac{2\pi f}{f_s}\, n + \varphi\right), \qquad x[n] = \sum_{h=1}^{H} a_h \sin\left(2\pi h f_0 \frac{n}{f_s}\right)$$

A sweep from $f_0$ to $f_1$ over $t_1 = (N-1)/f_s$, $x[n] = \cos \varphi(t_n)$:

$$\varphi_{\mathrm{lin}}(t) = 2\pi \left( f_0 t + \frac{f_1 - f_0}{2 t_1}\, t^2 \right)$$

$$\varphi_{\mathrm{log}}(t) = \frac{2\pi f_0 t_1}{\ln (f_1 / f_0)} \left( \left(\frac{f_1}{f_0}\right)^{t/t_1} - 1 \right)$$

Impulse and step at the point $n_0$:

$$x_{\delta}[n] = \begin{cases} 1, & n = n_0 \\ 0, & n \ne n_0 \end{cases} \qquad x_{u}[n] = \begin{cases} 0, & n < n_0 \\ 1, & n \ge n_0 \end{cases}$$

Amplitude and frequency modulation (carrier $\omega_c$, modulating frequency $\omega_m$, depth $m$, index $\beta$):

$$x_{\mathrm{AM}}[n] = \left(1 + m \sin \omega_m n\right) \sin \omega_c n, \qquad x_{\mathrm{FM}}[n] = \sin\left(\omega_c n + \beta \sin \omega_m n\right)$$

Colored noise: white Gaussian noise $x$, its DFT $X[k]$ is multiplied by $k^{\alpha}$, the zeroth harmonic is removed, then the signal is brought to the specified RMS $\rho$:

$$X[0] \leftarrow 0, \quad X[k] \leftarrow k^{\alpha} X[k], \quad x \leftarrow \frac{\rho\, x}{\sqrt{\tfrac{1}{N} \sum_n x[n]^2}}, \qquad \alpha = 0,\ -\tfrac{1}{2},\ -1$$

The exponent $\alpha$ for white, pink and brown noise: the spectral power is proportional to $k^{2\alpha}$, i.e. $1$, $1/k$ and $1/k^2$.

The Donoho–Johnstone signals are sampled at $t_i = (i+1)/N$; for example

$$x_{\mathrm{heavisine}}(t) = 4 \sin 4\pi t - \operatorname{sgn}(t - 0.3) - \operatorname{sgn}(0.72 - t), \qquad x_{\mathrm{doppler}}(t) = \sqrt{t(1-t)}\, \sin\frac{2\pi \cdot 1.05}{t + 0.05}$$

All frequencies must lie below $f_s / 2$, otherwise the frequency $f$ is indistinguishable from $f_s - f$ (§3.1).

## Algorithm and cost

1. A vector of $N = f_s T$ samples is filled in one pass; the phase of each sample is computed directly as $2\pi f k / f_s$ rather than accumulated, so there is no phase drift.
2. The sweep picks the phase formula by kind (linear or logarithmic); with $f_0 = f_1$ the logarithmic sweep degenerates into a pure tone, and with $N < 2$ a vector of ones is returned.
3. Noise: Gaussian numbers from mt19937_64 with the given seed; for pink and brown noise this is followed by a forward FFT, multiplication of the harmonics by $k^\alpha$, and an inverse FFT.
4. At the end the noise is scaled to the specified RMS.

Time $O(N)$ for the sine, sweep, AM and FM (one trigonometric function per sample), $O(H N)$ for $H$ harmonics, $O(N \log N)$ for colored noise. Memory $8N$ bytes. With the same seed the noise is reproduced bit for bit.

## With this block's numbers

Signal kind: {{block.kind}}; sampling rate $f_s$ = {{block.rate}}, duration $T$ = {{block.duration}}. Number of samples $N = f_s T$; the Nyquist frequency $f_s / 2$ is the upper bound for any frequency in the signal, and the frequency resolution of the spectrum is $f_s / N = 1/T$.

## Checks

This block has no numerical check in the interface: its correctness is pinned down by tests.

The tests compare the sweeps with scipy (`chirp`) and the Donoho–Johnstone signals with PyWavelets. For noise they check the spectral slope on a logarithmic scale (white, pink, brown), the exact RMS and reproducibility from the seed. The impulse, step, AM, FM and harmonics match the formulas to $10^{-15}$. Degenerate sweeps give a constant or an empty vector.

## Links

- §0.1 — the DTFT and the conversion $f = \omega f_s / 2\pi$ between hertz and rad/sample.
- §3.1 — aliasing: a frequency above $f_s/2$ is not reproduced as intended.
- §8.5 — impulse and step responses: the impulse and the step are needed precisely for them.
- §12.1 — the Donoho–Johnstone test signals for checking denoising.

## Implementation

The core of the sweep: the instantaneous phase of the linear and logarithmic chirp; the sine and the other kinds are the direct formulas above (`sine`, `am`, `fm`, `harmonics`).

```cpp
    const double t1 = static_cast<double>(n - 1) / fs;
    for (std::size_t k = 0; k < n; ++k) {
        if (core::stop_requested(progress, k)) return x;
        const double t = static_cast<double>(k) / fs;
        double ph = 0;
        if (kind == SweepKind::linear) {
            const double beta = (f1 - f0) / t1;
            ph = kTwoPi * (f0 * t + 0.5 * beta * t * t);
        } else if (f0 == f1) {
            ph = kTwoPi * f0 * t;
        } else {
            const double beta = t1 / std::log(f1 / f0);
            ph = kTwoPi * beta * f0 * (std::pow(f1 / f0, t / t1) - 1.0);
        }
        x[k] = std::cos(ph);
    }
```
