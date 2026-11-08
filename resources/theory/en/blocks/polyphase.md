+++
id = "polyphase"
title = "Polyphase split"
theory = ["conv-polyphase", "multirate-polyphase", "multirate-down-up"]
source = "src/pipeline/blocks_coeffs.cpp"
function = "polyphase"
test = "tests/pipeline/test_blocks_more2.cpp"
views = ["under_the_hood"]
+++

## Purpose

Split a signal into its even and odd samples and put it back together: `polyphase` gives two phases at half the rate, `ipolyphase` interleaves them into the original signal. This is the basis of the polyphase form of filter banks and fast decimators.

## Formulas

The phases of a signal of length $N$, as in §0.5:

$$e[n] = x[2n], \qquad o[n] = x[2n+1], \qquad X(z) = E(z^2) + z^{-1} O(z^2)$$

The lengths are $\lceil N/2\rceil$ and $\lfloor N/2\rfloor$, the rate of each phase is $f_s/2$. There is no filter, so each phase on its own contains aliasing:

$$E(z^2) = \frac{X(z) + X(-z)}{2}, \qquad z^{-1} O(z^2) = \frac{X(z) - X(-z)}{2}$$

On reassembly $y[2n] = e[n]$, $y[2n+1] = o[n]$ the terms with $X(-z)$ cancel, and $y = x$ exactly, with no rounding.

## Algorithm and cost

1. `polyphase`: one pass over each channel, an even-numbered sample goes to `even`, an odd-numbered one to `odd`; the outputs run at $f_s/2$.
2. `ipolyphase` checks that the phases have the same rate and channel count and that $\lvert e\rvert - \lvert o\rvert$ is 0 or 1; then it interleaves the samples, the output runs at $2 f_s$.

Cost $O(N)$ with no arithmetic; memory — a copy of the signal.

## With this block's numbers

No parameters: $N$ samples give phases of length $\lceil N/2\rceil$ and $\lfloor N/2\rfloor$. Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- Stereo $(1, 2, 3, 4, 5)$ and $(-1, -2)$ at 1000 Hz gives `even` $= (1, 3, 5), (-1)$ and `odd` $= (2, 4), (-2)$ at 500 Hz; `ipolyphase` returns the same channels and 1000 Hz.
- Splitting and reassembling the reference signal reproduces the input bit for bit.
- `ipolyphase` refuses phases of lengths 3 and 1, different channel counts and different rates.

## Links

- §0.5 — polyphase notation.
- §3.4 — polyphase decomposition for $M$ channels.
- §3.1 — decimation and interpolation: aliasing and images.
- The "Downsampling by M" and "Two-channel filter bank" cards.

## Implementation

Splitting one channel: even samples into one phase, odd ones into the other.

```cpp
    for (const auto& c : a.channels) {
        Samples e((c.size() + 1) / 2), o(c.size() / 2);
        for (std::size_t i = 0; i < c.size(); ++i) (i % 2 ? o[i / 2] : e[i / 2]) = c[i];
        even.channels.push_back(std::move(e));
        odd.channels.push_back(std::move(o));
    }
```
