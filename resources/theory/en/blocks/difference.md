+++
id = "difference"
title = "Difference and cumulative sum"
theory = ["lti-z-dtft", "lti-frequency-response"]
source = "src/pipeline/blocks_level.cpp"
function = "cumsum"
test = "tests/pipeline/test_blocks_more.cpp"
views = ["under_the_hood"]
+++

## Purpose

Discrete counterparts of the derivative and the integral: `diff` takes the difference of neighbouring samples, `cumsum` the running sum; each is the exact inverse of the other.

## Formulas

`diff` with a zero before the start, as `numpy.diff(x, prepend=0)` — the length $N$ is kept and $y[0] = x[0]$:

$$y[n] = x[n] - x[n-1], \quad x[-1] = 0, \qquad H(z) = 1 - z^{-1}, \qquad \lvert H(e^{i\omega})\rvert = 2\,\lvert\sin(\omega/2)\rvert$$

`cumsum`, as `numpy.cumsum`:

$$y[n] = \sum_{k=0}^{n} x[k], \qquad H(z) = \frac{1}{1 - z^{-1}}$$

Neither block multiplies or divides by $f_s$: the derivative estimate in units "per second" is $f_s\,y[n]$, the integral is $y[n]/f_s$.

`diff` is the simplest high-pass: a zero at DC, gain 2 at Nyquist. The pole $z = 1$ of `cumsum` lies on the unit circle: a constant offset $c$ grows linearly as $c\,(n+1)$.

Mutual inverses: $\mathrm{cumsum}(\mathrm{diff}(x)) = x$ and $\mathrm{diff}(\mathrm{cumsum}(x)) = x$ up to rounding — exactly because $x[-1] = 0$.

## Algorithm and cost

1. Each channel is traversed once; `diff` subtracts the previous sample (zero for the first one).
2. `cumsum` keeps the running sum in a `double` without compensation, so the rounding error grows roughly as $n$ machine epsilons of the size of the sum.

Cost $O(N)$ per channel, memory — one output buffer.

## With this block's numbers

No parameters: the output has the length and rate of the input. Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- `cumsum` matches `numpy.cumsum` to $10^{-12}$, `diff` matches `numpy.diff(x, prepend=0)` to $10^{-15}$.
- `cumsum` of `diff` returns the original signal to $10^{-12}$.
- On empty and one-sample inputs, on stereo and at $f_s = 0$ both blocks give a finite output or an error with a message.

## Links

- §1.2 — the z-transform and the DTFT.
- §1.3 — the frequency response.
- The "Recursive filters" card — `dc_block` combines `diff` with a "leaky" integrator $1/(1 - r z^{-1})$.

## Implementation

Both loops in a row: the difference with a zero before the start and the running sum.

```cpp
                                   for (std::size_t i = 0; i < c.size(); ++i) y[i] = c[i] - (i ? c[i - 1] : 0.0);
                                   return y;
                               });
                           }}));
    out.push_back(checked({"cumsum", kIn, kOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) {
                               return each_channel(audio(in), [](const Samples& c) {
                                   Samples y(c.size());
                                   double sum = 0;
                                   for (std::size_t i = 0; i < c.size(); ++i) y[i] = sum += c[i];
```
