+++
id = "listen"
title = "Playback"
theory = ["num-formats", "conv-signals"]
source = "src/audio/player.cpp"
function = "pull"
test = "tests/audio/test_player.cpp"
views = ["signal", "decomposition"]
+++

## Purpose

Play a signal or a fragment of it through the sound device to check the result by ear: what remains after zeroing a band, what the removed noise sounds like, how the signals before and after processing differ. The player processes nothing; it only hands samples to the device.

## Formulas

The device receives float32 samples: $\tilde x[n] = \operatorname{fl}_{32}(x[n])$ with a relative error of at most $u = 2^{-24} \approx 6 \cdot 10^{-8}$, i.e. about $-144$ dB relative to the sample itself. This is well below the 16-bit quantization step, which is why float32 is acceptable for the real-time path (§13.1).

A position $p$ in frames is converted to time by dividing by the sampling rate, and the duration of the clip $[p_0, p_1)$ by the difference of its bounds:

$$t = \frac{p}{f_s}, \qquad T_{\mathrm{clip}} = \frac{p_1 - p_0}{f_s}$$

A device frame with $C_o = \min(C, 2)$ channels is assembled from the first channels of the clip: extra channels of the clip are not played, and after the end of the clip the output is silence:

$$o[f C_o + c] = \begin{cases} x_c[p + f], & p + f < p_1 \\ 0, & p + f \ge p_1 \end{cases}, \qquad c = 0, \dots, C_o - 1$$

## Algorithm and cost

1. `play` validates the clip (not empty, non-zero rate, start inside the clip), chooses the end $p_1$ and stores the clip under a lock.
2. If the device is not running yet, or the rate or the number of channels has changed, it is restarted with the clip's parameters; the player does not resample on its own.
3. The audio thread calls `pull` to fill the buffer. The call does not block: if the main thread holds the lock or playback is stopped, the buffer is filled with zeros, and silence is heard instead of a stall.
4. `pull` copies the frames from position $p$ to the end of the clip, zeros the rest of the buffer and advances the position atomically. On reaching the end it raises a flag, and `poll` in the main thread switches the state to "finished".
5. `replace` swaps the clip on the fly while keeping the position — this is how A and B are compared without recomputing the location.

The cost of a `pull` call is $O(F\,C_o)$ per buffer of $F$ frames, without memory allocation or waiting. The clip is stored not as a copy but as float ranges with shared ownership: the memory is shared with the calling code.

## With this block's numbers

Clip: {{block.clip}}; sampling rate $f_s$ = {{block.rate}}. The current playback position $t = p / f_s$ = {{metric.position}} from the start of the signal; from it and the clip bounds you can see how much is left to play. For stereo, left and right samples alternate in the device buffer; for signals with three or more channels only the first two are heard.

## Checks

This block has no numerical check in the interface: its correctness is pinned down by tests.

The tests check that playback starts at the given frame and stops at the end frame or at the end of the clip, with silence after it. Stereo is interleaved; of many channels the first two are played. `replace` keeps the position for comparing A and B. After `stop` a new start restarts the device. A device failure puts the player into the "unavailable" state with a reason, an empty clip and a start past the end are rejected, and a race between `play` and `stop` and the audio thread is safe.

## Links

- §13.1 — formats and precision: float32 for the real-time path.
- §0.1 — the signal $x[n]$, the sampling rate and the conversion of samples to time.
- §3.5 — resampling: it would be needed if the device rate did not match the clip rate; the player does not resample on its own.
- The "Read WAV" block supplies the clip, and "Write WAV" saves what was listened to.

## Implementation

Filling the device buffer from the clip: channel interleaving, zeros past the end and an atomic advance of the position (a fragment of `pull`).

```cpp
        const auto channels = std::min<std::size_t>(clip.channels.size(), out_channels);
        std::size_t pos = position.load(std::memory_order_relaxed);
        std::size_t f = 0;
        for (; f < frames && pos < end; ++f, ++pos)
            for (std::size_t c = 0; c < out_channels; ++c) out[f * out_channels + c] = c < channels ? clip.channels[c][pos] : 0.0f;
        std::fill(out + f * out_channels, out + samples, 0.0f);
        position.store(pos, std::memory_order_relaxed);
        if (pos >= end) ended.store(true, std::memory_order_relaxed);
```
