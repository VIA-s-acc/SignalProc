# Filtering without delay

The signal `x` is the sum of 300 and 2000 Hz tones at $f_s = 8000$ Hz, the second one with amplitude {{a}}. Remove the 2000 Hz tone without shifting the 300 Hz tone in time. The output is node `y`. Do not change the nodes `low`, `high` and `x`.

## Goals {#goals}

1. The delay of `y` relative to `x` is 0 samples.
2. The 2000 Hz tone on `y` is weaker than on `x` by at least 40 dB.
3. The 300 Hz tone on `y` is weaker than on `x` by less than 1 dB.
4. The low tone reaches the output only through the mix `x`.
5. The high tone also goes only through the mix `x`.

## Hints {#hints}

1. An ordinary causal IIR filter always delays the signal: its phase is nonlinear. Raising the order does not remove the delay, it makes it longer.
2. The `iir` block can filter forward and backward: the `zero_phase` parameter; see {#cls-iir-prototypes}. The same can be built by hand: `iir`, then `reverse`, `iir` again and `reverse` again.
3. A forward-backward pass squares the magnitude response, so the attenuation in decibels doubles: an order-4 filter behaves like order 8 in depth of suppression.

## Solution {#solution}

`iir(family=butterworth, type=lowpass, order=4, cutoff=1000, zero_phase=yes)` works. Measured on the reference: delay {{m.lag}} samples, the 2000 Hz tone {{m.stop}} dB, the 300 Hz tone {{m.pass}} dB. The forward pass gives a phase shift $\varphi(\omega)$, the backward pass over the reversed signal gives $-\varphi(\omega)$, so the phases cancel and the magnitudes multiply: $|H|^2$. The same causal filter of order 4 gives a delay of 3 samples and a shallower attenuation, order 8 a delay of 6 samples. The price of zero phase: the filter is non-causal and needs the whole signal at once, so it does not work in real time. Details in {#classic}.
