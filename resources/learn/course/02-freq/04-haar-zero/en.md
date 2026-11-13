# The zero of the Haar H₀(z)

The transfer function of the Haar lowpass is $H_0(z) = \tfrac{1}{\sqrt2}\left(1 + z^{-1}\right)$. Where does its zero lie in the $z$-plane?

## Hints {#hints}

1. A zero is a value of $z$ with $H_0(z) = 0$, see {#freq-zero-pole}. Multiply by $z$: $1 + z^{-1} = 0$ means $z + 1 = 0$.
2. A point $z = e^{j\omega}$ on the unit circle is the frequency $\omega$. Which frequency does the point you found correspond to?
3. A zero at that frequency explains why a lowpass cuts the top of the band: look at the response on the «Filters» tab.

## Solution {#solution}

$1 + z^{-1} = 0$ gives $z = {{expected}}$. This is the point $e^{j\pi}$ on the unit circle: the Nyquist frequency $\omega = \pi$, where the magnitude response vanishes. A simple zero at $z = -1$ is the first vanishing moment of the wavelet; $\text{db}N$ has $N$ such zeros ({#wav-vanishing-moments}). A zero at $z = 1$ belongs to $H_1$, and in $H_0$ the delay $z^{-1}$ only puts a pole at $z = 0$.
