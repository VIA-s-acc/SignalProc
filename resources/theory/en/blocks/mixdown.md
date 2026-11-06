+++
id = "mixdown"
title = "Mix down to mono"
theory = ["conv-signals", "audio-stereo"]
source = "src/core/signal_ops.cpp"
function = "mixdown"
test = "tests/core/test_signal_ops.cpp"
views = ["signal"]
+++

## Purpose

Mix a multichannel signal down to one channel by averaging the channels sample by sample. Needed when one channel is enough: for the spectrum, the wavelet decomposition, listening in mono.

## Formulas

$$m[n] = \frac{1}{C} \sum_{c=0}^{C-1} x_c[n]$$

Dividing by $C$ guarantees that the result does not exceed the inputs: $\lvert m[n] \rvert \le \max_c \lvert x_c[n] \rvert$, so signals within full scale yield a signal within it.

The level depends on how the channels are related. Identical channels ($x_0 = x_1 = x$) give $m = x$ with no change of level. Uncorrelated channels of equal power $\sigma^2$ give power $\sigma^2 / C$, i.e. a level drop of $10 \log_{10} C$ dB. Channels in antiphase ($x_1 = -x_0$) cancel each other:

$$m[n] = \frac{x_0[n] + x_1[n]}{2} = 0 \quad \text{for } x_1 = -x_0$$

The stereo mid channel $M = \tfrac12(L+R)$ is exactly such a mix (§12.6).

## Algorithm and cost

1. For $C$ channels a vector of sums is allocated, as long as the number of frames of the first channel.
2. The channels are added to the sum one by one; a shorter channel is padded with zeros on the right, and a longer one is truncated to the length of the first.
3. The sum is divided by $C$, and the result is packaged as one channel with the same sampling rate.

Time $O(N\,C)$, memory $O(N)$. An empty set of channels is returned empty. Channels of different length do not occur in normal data; the code tolerates them rather than checking for them.

## With this block's numbers

The input signal has $C = {{block.channels}}$ channels; the weight of each channel in the mix is $1 / C$. For uncorrelated channels of equal power the level shifts by $-10 \log_{10} {{block.channels}}$ dB (with $C = 1$ by zero: the channel is copied), and for identical channels it stays the same.

## Checks

This block has no numerical check in the interface: its correctness is pinned down by tests.

On the channel pair $(1, 3)$ and $(3, 5)$ the test checks the result $(2, 4)$, a single output channel and that the sampling rate is preserved.

## Links

- §12.6 — stereo: mid/side, per-channel processing.
- §0.1 — the signal $x[n]$ and linear operations on it.
- The "Split channels" block — the opposite action: from a multichannel signal to a set of mono signals.
- The "Mid/side" block keeps both the sum and the difference of the channels, so nothing is lost.

## Implementation

Summing the channels sample by sample and dividing by their number.

```cpp
AudioBuffer mixdown(const AudioBuffer& audio) {
    AudioBuffer out{audio.sample_rate, {}};
    if (audio.channels.empty()) return out;
    std::vector<double> sum(audio.frames(), 0.0);
    for (const auto& c : audio.channels)
        for (std::size_t k = 0; k < sum.size() && k < c.size(); ++k) sum[k] += c[k];
    for (double& v : sum) v /= static_cast<double>(audio.channels.size());
    out.channels.push_back(std::move(sum));
    return out;
}
```
