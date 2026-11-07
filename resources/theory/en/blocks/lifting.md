+++
id = "lifting"
title = "Lifting 5/3 and 9/7"
theory = ["lift-53", "lift-97", "lift-benefits"]
source = "src/core/lifting.cpp"
function = "lift53_int"
test = "tests/core/test_lifting.cpp"
views = ["under_the_hood"]
+++

## Purpose

Split a signal into bands with in-place predict and update steps instead of convolutions. The integer 5/3 (JPEG 2000) restores integer samples bit for bit, which is lossless compression.

## Formulas

LeGall 5/3 with summing normalisation:

$$d[n]=x[2n+1]-\tfrac12\big(x[2n]+x[2n+2]\big), \qquad s[n]=x[2n]+\tfrac14\big(d[n-1]+d[n]\big)$$

The integer version:

$$d[n]=x[2n+1]-\left\lfloor\frac{x[2n]+x[2n+2]}{2}\right\rfloor, \qquad s[n]=x[2n]+\left\lfloor\frac{d[n-1]+d[n]+2}{4}\right\rfloor$$

CDF 9/7 is four steps $\alpha,\beta,\gamma,\delta$ and a scale $\zeta$: $d\leftarrow d+\alpha(s_n+s_{n+1})$, $s\leftarrow s+\beta(d_{n-1}+d_n)$ and so on.

## Algorithm and cost

1. Even samples become $s$, odd ones $d$; edges mirror the indices (WS), so any length works, including 1 and 2.
2. The inverse runs the same steps backwards with opposite signs, so integer rounding cancels exactly.
3. Integer coefficients are kept in 64 bits; more than 30 levels are refused, so overflow cannot happen.
4. The 9/7 constants are fitted by Newton's method to the `bior4.4` taps: the published 10 digits are off by about $3\cdot10^{-10}$.

Cost $O(n)$ per level: 4 additions and 2 multiplications per sample pair for 5/3.

## With this block's numbers

Levels: {{block.levels}}; integer mode: {{block.integer}}.

## Checks

- Real 5/3 matches the LeGall convolution inside the signal to $10^{-12}$; 5/3 and 9/7 round trips are exact to $10^{-12}$ for any length.
- Integer 5/3 restores int16 and int32 bit for bit, including the extremes; so do 10 levels on int32.
- The 9/7 equivalent lowpass matches `bior4.4` to $10^{-12}$, the highpass to $10^{-11}$.

## Links

- §10.3 — LeGall 5/3 and the integer version.
- §10.4 — CDF 9/7.
- §10.5 — benefits and pitfalls of lifting.

## Implementation

Integer prediction with rounding down.

```cpp
    for (std::size_t i = 1; i < n; i += 2) out.d.push_back(x[i] - floor_div(x[i - 1] + even_at(x, static_cast<std::ptrdiff_t>(i) + 1), 2));
```
