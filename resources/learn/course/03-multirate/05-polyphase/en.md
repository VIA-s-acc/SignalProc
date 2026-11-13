# Polyphase convolution without ↓2

Produce $y = \downarrow 2\,(h * x)$: the convolution of the signal `x` with the filter $h$, keeping the even-numbered samples. The filter has four taps: $h_0 = {{h0}}$, $h_1 = {{h1}}$, $h_2 = {{h2}}$, $h_3 = {{h3}}$. It has to be computed in the polyphase way: the signal and the filter are split into even and odd parts, all filters run at the half rate, and there is no `downsample` block in the graph. The output is node `y`. Do not change node `x`: the check feeds other signals into it too.

The start graph already splits the signal with a `polyphase` block and has two filters of two taps: the even taps $h_0, h_2$ for the even part of the signal, the odd taps $h_1, h_3$ for the odd part. The result is not right yet.

## Goals {#goals}

1. The output `y` equals $\downarrow 2\,(h * x)$ to $10^{-12}$ — on your signal and on the hidden ones.
2. The graph has no `downsample`, `resample` or ready-made wavelet blocks.
3. Every path from `x` to `y` goes through a `polyphase` block.

## Hints {#hints}

1. The output $(h*x)[2n] = \sum_j h[j]\,x[2n-j]$ splits into even taps $j = 2k$ and odd taps $j = 2k+1$. The even taps multiply $x[2(n-k)]$, the even part of the signal.
2. The odd taps multiply $x[2(n-k)-1]$, which is the odd part $x[2m+1]$ with a shift: $m = n - k - 1$. So the odd branch has to be delayed by one sample at the half rate.
3. A `delay` block with `samples=1` goes in front of the filter `fo` in the odd branch; the branches are summed by an `add` block.

## Solution {#solution}

The graph: `x → polyphase`; `even → fir_taps({{h0}} {{h2}}) → add.a`; `odd → delay(1) → fir_taps({{h1}} {{h3}}) → add.b`.

The type-1 polyphase split of the filter is $H(z) = E_0(z^2) + z^{-1}E_1(z^2)$, of the signal $X(z) = X_0(z^2) + z^{-1}X_1(z^2)$. The even samples of the product are $Y(z) = E_0(z)X_0(z) + z^{-1}E_1(z)X_1(z)$. The delay $z^{-1}$ in the odd branch appears because $z^{-1} \cdot z^{-1} = z^{-2}$ is already a shift by a whole sample of the half rate. There are half as many multiplications as for ↓2 after the full convolution: the discarded samples are never computed. See {#multirate-polyphase} and {#conv-polyphase}.
