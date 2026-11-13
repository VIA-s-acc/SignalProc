# The ratio for 44 100 → 48 000

A recording at 44 100 Hz has to be resampled to 48 000 Hz with the scheme $\uparrow L \to H \to \downarrow M$. Find the integers $L$ and $M$ in lowest terms: two numbers separated by a space, $L$ first, then $M$.

## Hints {#hints}

1. The new rate is $f_s \cdot L / M$, so $L / M = 48000 / 44100$.
2. Cancel the fraction by the greatest common divisor: both numbers are divisible by 300.
3. A check: a `resample` block with the $L$ and $M$ you found, fed a tone at 44 100 Hz, outputs 48 000 Hz.

## Solution {#solution}

$L = 160$, $M = 147$: $48000 / 44100 = 160 / 147$, since $\gcd(48000, 44100) = 300$.

Lowest terms matter: the intermediate rate is $44100 \cdot 160 = 7.056$ MHz, and any common factor would only lengthen the filter $H$ for nothing. The scheme and its polyphase implementation are in {#multirate-rational}.
