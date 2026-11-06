+++
id = "upsample"
title = "Upsampling by M"
theory = ["multirate-down-up"]
source = "src/core/multirate.cpp"
function = "interpolate"
test = "tests/core/test_multirate.cpp"
views = ["signal"]
+++

## Purpose

Raise the sampling rate by a factor of $M$: insert $M-1$ zeros between the samples and smooth with a lowpass filter, removing the mirror images of the spectrum.

## Formulas

$${\uparrow}M:\quad y[n] = \begin{cases} x[n/M], & M \mid n \\ 0, & \text{otherwise} \end{cases} \qquad Y(z) = X(z^M)$$

The spectrum is compressed by a factor of $M$, and $M-1$ mirror images (imaging) appear at the frequencies $2\pi k/M$. The interpolator is ${\uparrow}M$ followed by a lowpass filter with cutoff $\pi/M$ and gain $M$:

$$y = g * ({\uparrow}M\, x), \qquad g = M\, h, \qquad \sum_m g[m] = M$$

The gain $M$ is needed because inserting zeros reduces the mean by a factor of $M$: the filter restores the amplitude.

## Algorithm and cost

1. The factor $M = 0$ is an error; without the filter the result is the signal with zeros between the samples.
2. The samples are placed with step $M$ into an array of length $N M$.
3. A Kaiser filter with cutoff $\pi/M$ (as in the decimator) is multiplied by $M$; its delay $(L-1)/2$ is compensated.
4. Convolution (§1.1); the result is trimmed to exactly $N M$ samples.

Cost $\approx N M L$ multiplications (long factors go through the FFT): the whole zero-stuffed array is filtered; the polyphase form (§3.4) would cost $N L$ — the zeros are not multiplied.

## With this block's numbers

$M = {{block.m}}$: each sample is followed by $M - 1 = {{block.m}} - 1$ zeros, and the spectrum is $Y(z) = X(z^{ {{block.m}} })$.

A filter of $L = {{block.taps}}$ taps with gain $M$ at zero frequency; the delay of $(L-1)/2 = ({{block.taps}} - 1)/2$ samples is compensated.

## Checks

It would be worth checking that without the filter zero insertion is exact ($[1, 2] \to [1, 0, 0, 2, 0, 0]$), and that the chain "interpolation, then decimation" returns a band-limited signal within the ripple — these checks live in `tests/core/test_multirate.cpp`; they are not on screen.

## Links

- §3.1 — the spectrum after ${\uparrow}M$: mirror images.
- §3.2 — the interpolator: cutoff $\pi/M$ and gain $M$.
- §3.4 — a polyphase interpolator without multiplications by zeros.
- §4.1 — in a filter bank ${\uparrow}2$ is part of synthesis, and aliasing is cancelled by the second channel.
- §11.1 — the inverse DWT: ${\uparrow}2$ and the synthesis filters.

## Implementation

Zero insertion, a lowpass filter with gain $M$ and trimming to $N M$ samples, accounting for the filter delay.

```cpp
std::vector<double> stuffed(x.size() * m, 0.0);
for (std::size_t k = 0; k < x.size(); ++k) stuffed[k * m] = x[k];
if (m == 1 || !options.anti_alias || x.empty()) return stuffed;
auto h = anti_alias_filter(m, options);
for (double& c : h.coeffs) c *= static_cast<double>(m);
const auto filtered = convolve(Laurent{std::move(stuffed), 0}, h);
const std::size_t d = filter_delay(h, options);
return std::vector<double>(filtered.coeffs.begin() + static_cast<std::ptrdiff_t>(d),
                           filtered.coeffs.begin() + static_cast<std::ptrdiff_t>(d + x.size() * m));
```
