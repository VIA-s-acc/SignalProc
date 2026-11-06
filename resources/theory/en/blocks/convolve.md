+++
id = "convolve"
title = "Convolution — direct and via FFT"
theory = ["lti-convolution", "toeplitz-circulant"]
source = "src/core/conv.cpp"
function = "convolve"
test = "tests/core/test_conv.cpp"
views = ["filters", "matrix"]
+++

## Purpose

Apply an FIR filter $h$ to a signal $x$: the basic operation of every filter bank, from which analysis and synthesis are built.

## Formulas

Linear convolution of signals of length $N$ and $L$ gives $N + L - 1$ samples:

$$y[n] = \sum_k h[k]\, x[n-k]$$

The same operation is multiplication by a banded Toeplitz matrix (§2.2), $(T_h)_{ij} = h[i-j]$:

$$y = T_h\, x, \qquad T_h \in \mathbb R^{(N+L-1)\times N}$$

Circular convolution of length $N$ and its diagonalization by the DFT (§2.3):

$$(h \circledast x)[n] = \sum_k h[k]\, x[(n-k) \bmod N], \qquad h \circledast x = F^{-1}\big(\hat h \cdot \hat x\big)$$

Linear convolution coincides with circular convolution if both signals are zero-padded to a length $\ge N + L - 1$. In Laurent polynomials the starting index of the result equals the sum of the starting indices of the factors.

## Algorithm and cost

1. The "direct" method: a double loop over the coefficients, $O(NL)$ multiplications.
2. The "FFT" method: zero-padding to a convenient length $n \ge N + L - 1$, two forward and one inverse real FFT, element-wise product of the spectra — $O(n \log n)$.
3. Automatic choice: FFT if $\min(N, L) \ge 160$ (the constant `fft_threshold`), otherwise the direct method.

Memory $O(N + L)$. The error of FFT convolution, of order $u \log n\,\|h\|\,\|x\|$, is absolute rather than relative to each sample: small outputs get a large relative error.

## With this block's numbers

Method: {{block.method}}; $N = {{block.n}}$, $L = {{block.filter_length}}$.

Output length $N + L - 1 = {{block.n}} + {{block.filter_length}} - 1$; the direct method takes $N L = {{block.n}} \cdot {{block.filter_length}}$ multiplications.

The chosen method takes ≈ {{metric.multiplies}} multiplications.

## Checks

It would be worth checking that direct convolution, FFT convolution and multiplication by the Toeplitz matrix agree to within $10^{-13}\|h\|_1\|x\|_\infty$, and that circular convolution equals linear convolution after folding modulo $N$ — these checks live in `tests/core/test_conv.cpp`; they are not on screen.

## Links

- §1.1 — linear and circular convolution and correlation; algorithms and pitfalls.
- §2.2 — convolution as a banded Toeplitz matrix.
- §2.3 — circulants and diagonalization by the DFT.
- §2.5 — fast multiplication via embedding in a circulant.
- §3.4 — polyphase form: convolution without computing the discarded samples.
- §11.1 — the DWT is convolution with $h_0$, $h_1$ followed by downsampling.

## Implementation

Choice of method by the lengths of the factors and a call to the direct or FFT variant.

```cpp
Laurent convolve(const Laurent& a, const Laurent& b, ConvMethod method) {
    if (a.coeffs.empty() || b.coeffs.empty()) return {{}, a.offset + b.offset};
    if (method == ConvMethod::automatic)
        method = std::min(a.coeffs.size(), b.coeffs.size()) >= fft_threshold ? ConvMethod::fft : ConvMethod::direct;
    return method == ConvMethod::fft ? convolve_fft(a, b) : multiply(a, b);
}
```
