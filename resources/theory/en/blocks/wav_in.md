+++
id = "wav_in"
title = "Read WAV"
theory = ["num-formats", "conv-signals"]
source = "src/io/wav.cpp"
function = "read_wav"
test = "tests/io/test_wav_read.cpp"
views = ["signal"]
+++

## Purpose

Read a WAV file into a double signal $x_c[n]$: one array per channel, on the scale $[-1, 1)$ regardless of the source format (PCM 8/16/24/32-bit, float32/64, A-law, $\mu$-law). Along the way, compute the duration, the peak and the number of samples at full scale.

## Formulas

Signed integer PCM of $b$ bits is divided by $2^{b-1}$; 8-bit (unsigned) PCM is first shifted by 128:

$$x[n] = \frac{q[n]}{2^{b-1}}, \quad -2^{b-1} \le q[n] \le 2^{b-1}-1, \qquad x_{\mathrm{u8}}[n] = \frac{q[n]-128}{128}$$

Float32 and float64 are carried over as is, $x[n] = q[n]$; A-law and $\mu$-law are first expanded to 16 bits, then $x[n] = q_{16}[n] / 2^{15}$.

Duration and peak of channel $c$:

$$T = \frac{N}{f_s}, \qquad P_c = 20 \log_{10} \max_n \lvert x_c[n] \rvert \;\text{dBFS}$$

Full scale is $x_{\mathrm{fs}} = 1 - 2^{1-b}$ for integer formats and $1$ for float; the clipping counter $K_c$ is the number of samples with $\lvert x_c[n] \rvert \ge x_{\mathrm{fs}}$.

## Algorithm and cost

1. The header is parsed by the dr_wav library (RIFF and extensible); the format is determined by the pair "tag, bit count". An unsupported pair gives an error, not a crash.
2. The data is read in blocks of $2^{16}$ frames. In the file the channels are interleaved, $s[nC + c] = x_c[n]$; each sample is decoded to double and placed into its channel.
3. After each block progress is reported and cancellation is checked.
4. An empty file and a file without channels are errors.
5. One pass over each channel gives the peak, RMS, DC component and $K_c$.

Time $O(N\,C)$, memory $8\,N\,C$ bytes: a 16-bit file takes four times more space in memory than on disk. At most $2^{24}$ frames per channel are reserved, so that a corrupted header cannot allocate gigabytes.

## With this block's numbers

File {{block.path}}: format {{block.format}}, $C = {{block.channels}}$ channels, sampling rate {{block.rate}}, {{block.frames}} frames; $T = N / f_s$ = {{block.duration}}. In memory this is $8\,N\,C$ bytes of double.

Peak per channel: {{metric.peak_dbfs}}; samples at full scale: {{metric.clipped}}. If the peak is near zero and the counter is non-zero, the file was already clipped when it was written: the shape of the peaks is lost, and gain will only increase the distortion. Reading took {{metric.load_time}}.

## Checks

This block has no numerical check in the interface: its correctness is pinned down by tests.

The tests read reference files of all formats and compare the format, the number of channels, the rate, the number of frames and the samples themselves (bit for bit) with the reference. Corrupted files give an error, not a crash. The two channels of `test.wav` stay separate. Reading reports progress and can be cancelled. `probe_wav` reads only the header.

## Links

- §13.1 — formats and precision: double for processing, float32 only for display buffers.
- §0.1 — the signal $x[n]$, the frequency $f = \omega f_s / 2\pi$.
- §12.6 — WAV formats are converted to double in $[-1, 1)$, after which processing goes channel by channel.
- The "Write WAV" block — the inverse operation: quantization and dithering are there too.

## Implementation

Decoding integer PCM samples to the scale $[-1, 1)$: `read_wav` calls these functions for each sample of a block.

```cpp
double decode_pcm_u8(const unsigned char* p) { return (static_cast<double>(p[0]) - 128.0) / 128.0; }

double decode_pcm_s16(const unsigned char* p) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0] | p[1] << 8)) / 32768.0;
}

double decode_pcm_s24(const unsigned char* p) {
    auto v = static_cast<std::int32_t>(std::uint32_t{p[0]} | std::uint32_t{p[1]} << 8 | std::uint32_t{p[2]} << 16);
    if (v & 0x800000) v -= 0x1000000;
    return v / 8388608.0;
}

double decode_pcm_s32(const unsigned char* p) { return static_cast<std::int32_t>(le32(p)) / 2147483648.0; }
```
