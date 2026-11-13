# Eigenvalues of a circulant

The filter `h = [{{a}} {{b}} {{c}}]`, padded with a zero to length $N = 4$, defines a circulant $C(h)$ of size $4 \times 4$: circular convolution with $h$. Find the magnitudes of its eigenvalues $|\lambda_0|, |\lambda_1|, |\lambda_2|, |\lambda_3|$ in frequency order $k = 0, 1, 2, 3$. The answer is four numbers separated by spaces; expressions such as `sqrt(5)` are accepted. Tolerance $10^{-6}$.

## Hints {#hints}

1. One and the same Fourier transform diagonalizes every circulant, and the eigenvalues of $C(h)$ are the DFT of the vector $h$: $\lambda_k = \sum_n h[n]\, e^{-2\pi i kn/N}$; see {#toeplitz-circulant}.
2. For $N = 4$ the factors $e^{-2\pi i kn/4}$ take the values $1, -i, -1, i$. For $k = 0$ this is the sum of samples, for $k = 2$ the alternating sum; for $k = 1$ and $k = 3$ you need the magnitude of a complex number.
3. A circulant is normal, so its singular values are the same magnitudes $|\lambda_k|$. The "Matrix" tab shows them in descending order (key `s`), but you have to restore the order by $k$ yourself.

## Solution {#solution}

The answer is {{expected}} (the magnitudes for $k = 0, 1, 2, 3$). Here $\lambda_0 = h_0 + h_1 + h_2$, $\lambda_2 = h_0 - h_1 + h_2$, and $\lambda_{1,3} = h_0 - h_2 \mp i\,h_1$, so $|\lambda_1| = |\lambda_3| = \sqrt{(h_0 - h_2)^2 + h_1^2}$. A real filter gives a conjugate pair at $k$ and $N - k$ with equal magnitudes. The eigenvalues of a circulant are the values of the frequency response $H(e^{i\omega})$ at $N$ equally spaced points: the spectrum of the matrix and the frequency response of the filter are the same thing. See {#toeplitz-circulant} and {#fbm-periodization}.
