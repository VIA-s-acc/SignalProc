+++
id = "phi_psi"
title = "Spectra of φ and ψ"
theory = ["freq-phi-psi-spectra", "freq-heisenberg"]
source = "src/core/wavelet/spectra.cpp"
function = "scaling_spectrum"
test = "tests/core/test_spectra.cpp"
views = ["filters"]
+++

## Purpose

See the scaling function and the wavelet in the frequency domain: $\lvert\Phi\rvert$ is a lowpass with zeros at $2\pi k$, $\lvert\Psi\rvert$ is a bandpass peaking in the octave $[\pi, 2\pi]$. For the CWT, $\hat\Psi(a\omega)$ at several scales is a bank of constant-Q bandpass filters.

## Formulas

$$\Phi(\omega)=\prod_{j=1}^{\infty} m_0(\omega/2^j), \qquad m_0=\frac{H(e^{i\omega})}{H(1)}$$

$$\Psi(\omega)=m_1(\omega/2)\,\Phi(\omega/2), \qquad m_1=\frac{G(e^{i\omega})}{H(1)}$$

For $\varphi$ we take $H=f_0$ and $G=f_1$; for the dual $\tilde\varphi$, the reversed $h_0$ and $h_1$.

A zero of order $N$ of $m_0$ at $\pi$ gives zeros of $\Phi$ of the same order at $2\pi k$, $k\neq0$ (the Strang–Fix condition). Near zero $\lvert\Psi(\omega)\rvert\sim\lvert\omega\rvert^{\tilde N}$, a slope of $20\tilde N$ dB per decade on a log axis.

## Algorithm and cost

1. Check $H(-1)=0$. Without a zero at $-1$ the product does not decay and $\varphi\notin L^2$, so the spectrum is refused with the reason.
2. Multiply $J$ factors until $2^{-J}\lvert\omega\rvert<10^{-9}$: about 35 for $\omega\le8\pi$. Each factor is a polynomial value by Horner's scheme.
3. Replace the dropped tail by $e^{-i\tau\omega 2^{-J}}$, where $\tau$ is the filter's centre of mass. This removes the phase error of truncation.
4. The o and O keys change the number of factors: $\Phi_J$ shows the cascade converging in the frequency domain.

Cost $O(PJL)$ for $P$ points and a filter of length $L$.

## With this block's numbers

Spectrum: {{block.filter}}; factors: {{block.factors}}.

Zero order: {{metric.zeros}}; partition of unity: {{metric.partition}}.

## Checks

- Haar: $\Phi(\omega)=(1-e^{-i\omega})/(i\omega)$ to $10^{-12}$, $\lvert\Psi(\omega)\rvert=4\sin^2(\omega/4)/\omega$.
- db4: $\sum_k\lvert\Phi(\omega+2\pi k)\rvert^2=1$ over $\lvert k\rvert\le400$ to $10^{-6}$; db3: $\sum_j\lvert\Psi(2^j\omega)\rvert^2=1$.
- $\Phi(2\pi k)=0$ for $k\neq0$ for db2, sym5, bior2.2 and coif2.
- $\hat\Psi$ for morl, mexh and cmor matches the numerical transform of $\psi$ to $10^{-9}$.

## Links

- §8.9 — spectra of φ and ψ.
- §8.10 — time–frequency tiling.

## Implementation

The product with the tail correction.

```cpp
        std::complex<double> p = std::polar(1.0, -centre * std::ldexp(omega[k], -static_cast<int>(count)));
        for (std::size_t j = 1; j <= count; ++j) p *= dtft(lowpass, std::ldexp(omega[k], -static_cast<int>(j))) / gain;
```
