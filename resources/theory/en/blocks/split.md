+++
id = "split"
title = "Split channels"
theory = ["conv-signals", "audio-stereo"]
source = "src/core/signal_ops.cpp"
function = "select_channel"
test = "tests/core/test_signal_ops.cpp"
views = ["signal"]
+++

## Purpose

Split a multichannel signal into independent mono signals, one per channel, so that each can be processed by its own chain of blocks. The sampling rate and the number of samples stay the same in all branches.

## Formulas

A multichannel signal is a set $x_0[n], \dots, x_{C-1}[n]$ of the same length $N$; branch $c$ receives its channel unchanged:

$$y_c[n] = x_c[n], \qquad c = 0, \dots, C-1, \quad n = 0, \dots, N-1$$

In a WAV file the samples are interleaved by frames, $s[nC + c] = x_c[n]$; splitting is the inverse of this interleaving. The operation does not change the values, so it is exact: no rounding at all.

Per-channel processing with the same filters and levels gives aligned channel coefficients; for stereo, thresholds and subband masks are usually shared, to preserve the stereo image (§12.6).

## Algorithm and cost

1. It is checked that the requested channel exists; otherwise an error with the channel index and the number of channels.
2. The selected vector is copied into a new mono buffer with the same sampling rate.
3. Branch $c$ is the result of `select_channel(audio, c)`; for all channels the call is repeated with indices $0, \dots, C-1$.

There is no separate "split" function in the code: splitting comes down to selecting a channel. Time and memory per branch $O(N)$ (a copy of the vector); all $C$ branches together take as much memory as the input.

## With this block's numbers

The input signal has $C = {{block.channels}}$ channels; so the output is $C$ independent mono signals, each of length $N$ and with the same sampling rate $f_s$. For stereo these are left and right; with $C = 1$ the block simply passes the single channel on.

## Checks

This block has no numerical check in the interface: its correctness is pinned down by tests.

The test checks that the selected channel matches the original (for the stereo pair $(1, 3)$ and $(3, 5)$ channel 1 gives $(3, 5)$), and that a request for a non-existent channel returns an error.

## Links

- §12.6 — stereo: per-channel processing, the same filters and levels, one mask for both channels.
- §0.1 — a signal $x[n]$ of finite length $N$.
- The "Mix down to mono" block assembles the channels back into one by averaging.
- The "Mid/side" block — an alternative to per-channel processing for stereo.

## Implementation

There is no separate "split" function; the closest real code is `select_channel`, which takes the requested channel into a new mono buffer.

```cpp
Result<AudioBuffer> select_channel(const AudioBuffer& audio, std::size_t index) {
    if (index >= audio.channels.size())
        return make_error(Errc::invalid_argument, "channel " + std::to_string(index) + " does not exist (" +
                                                      std::to_string(audio.channels.size()) + " channels)");
    return AudioBuffer{audio.sample_rate, {audio.channels[index]}};
}
```
