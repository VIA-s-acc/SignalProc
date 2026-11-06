+++
id = "wav_out"
title = "Write WAV"
theory = ["num-formats", "met-snr"]
source = "src/io/wav.cpp"
function = "write_wav"
test = "tests/io/test_wav_write.cpp"
views = ["signal"]
+++

## Purpose

Write a signal to a WAV file as integer PCM (16, 24 or 32 bits) or float32. Integer writing quantizes the samples, optionally adds dither, and counts how many samples exceeded the scale and were clipped.

## Formulas

Quantization to $b$ bits with clipping (saturation) and dither $\delta$:

$$q[n] = \min\left(2^{b-1}-1,\; \max\left(-2^{b-1},\; \operatorname{round}\left(2^{b-1}\,x[n] + \delta[n]\right)\right)\right)$$

$$\delta[n] = u_1[n] - u_2[n], \qquad u_1,\, u_2 \text{ independent and uniform on } [0, 1)$$

The difference of two uniform variables has a triangular density on $(-1, 1)$ steps (TPDF dither); without dither $\delta = 0$. The quantization step is one least significant bit, $\Delta = 1$. Quantization noise power in units of $\Delta^2$:

$$\sigma_0^2 = \frac{1}{12}, \qquad \sigma_{\mathrm{TPDF}}^2 = \frac{1}{12} + \frac{1}{6} = \frac{1}{4}$$

Signal-to-noise ratio for a full-scale sine:

$$\mathrm{SNR}_0 \approx 6.02\,b + 1.76 \;\text{dB}, \qquad \mathrm{SNR}_{\mathrm{TPDF}} \approx 6.02\,b - 3.01 \;\text{dB}$$

Float32 is neither quantized nor clipped: $y[n] = \operatorname{fl}_{32}(x[n])$, $\lvert y - x \rvert \le 2^{-24} \lvert x \rvert$.

## Algorithm and cost

1. A check before writing: 1 to 256 channels of the same length, a positive integer sampling rate, all samples finite. Otherwise an error, and the file is not created.
2. The format is mapped to a pair (tag, bit count): pcm_s16, pcm_s24, pcm_s32 and float32 are supported.
3. Frames go in blocks of $2^{16}$; within a frame the channel samples follow one another, each is scaled by $2^{b-1}$, dithered, rounded, clipped and written in little-endian order.
4. Exceeding the scale causes saturation and increments the counter of clipped samples; there is no overflow wrap-around.
5. If writing fails, only the file created by this call is removed; someone else's file at the same path stays.

Time $O(N\,C)$, memory — one block of $2^{16}\,C\,b/8$ bytes. Dither consumes two mt19937_64 numbers per sample; the seed is fixed, so two runs produce the same file.

## With this block's numbers

Format {{block.format}}, TPDF dither: {{block.dither}}. The quantization step is $\Delta = 2^{1-b}$ of full scale, where $b$ is the bit depth of the format. Without dither the noise has power $\Delta^2/12$ and is correlated with the signal; with dither it is $\Delta^2/4$, but independent of the signal.

Samples clipped: {{metric.clipped}}. Non-zero means the signal went outside $[-1, 1)$; the clipped samples are distorted, and they should have been reduced by gain or normalization before writing.

## Checks

This block has no numerical check in the interface: its correctness is pinned down by tests.

The tests check that integer formats return the signal to within half a least significant bit, and float32 to float precision. Exceeding the scale is clipped and counted, but does not wrap around. Non-finite samples and invalid buffers or formats are rejected before writing, and no file appears. Writing is 24-bit by default. Dither is deterministic for a given seed, small in amplitude and does not shift the mean. A failed write does not remove an already existing file. High sampling rates and the maximum number of channels survive a round trip.

## Links

- §13.1 — formats and precision: float32 with $u = 2^{-24}$, double for processing.
- §14.2 — SNR: this is how quantization noise is measured.
- §0.1 — the signal $x[n]$ and its normalization to $[-1, 1)$.
- The "Read WAV" block — the inverse operation; the "Gain in dB" and "Level normalization" blocks set the level before writing.

## Implementation

Quantization of one sample: scaling, TPDF dither, rounding and saturation with a clip counter.

```cpp
        double v = x * scale_;
        if (dither_) {
            const double d1 = uniform_(rng_);
            const double d2 = uniform_(rng_);
            v += d1 - d2;
        }
        double q = std::round(v);
        if (q < -scale_) {
            q = -scale_;
            ++clipped_;
        } else if (q > scale_ - 1.0) {
            q = scale_ - 1.0;
            ++clipped_;
        }
        put(static_cast<std::uint64_t>(static_cast<std::int64_t>(q)), out);
```
