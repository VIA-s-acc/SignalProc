+++
id = "correlation"
title = "Correlation and delay"
theory = ["lti-convolution", "pitch-yin"]
source = "src/pipeline/blocks_analysis.cpp"
function = "delay_estimate"
test = "tests/pipeline/test_blocks_analysis.cpp"
views = ["under_the_hood"]
+++

## Purpose

Compare two signals: `xcorr` gives the cross-correlation over all lags, `delay_estimate` finds from its peak how far $b$ lags behind $a$ (to a fraction of a sample), `correlation` reduces the similarity to three numbers — Pearson, Spearman and cosine.

## Formulas

`xcorr` follows `scipy.signal.correlate(a, b)`; the `full` mode gives every lag:

$$r[\ell] = \sum_n a[n+\ell]\,b[n], \qquad \ell = -(N_b-1), \dots, N_a-1$$

`same` keeps the central $N_a$ values, `valid` only the lags where the shorter signal lies wholly inside the longer one ($\lvert N_a - N_b\rvert + 1$ values). With `normalize = yes` everything is divided by $\sqrt{E_a E_b}$, $E = \sum_n x[n]^2$ over the whole input, so $\lvert r\rvert \le 1$ and the autocorrelation at zero lag is 1.

`delay_estimate` takes the same correlation of $b$ against $a$, the peak in magnitude $k = \arg\max_\ell \lvert r[\ell]\rvert$ (the first of equal ones) and a parabola through three points:

$$\delta = \frac{1}{2}\,\frac{\lvert r[k-1]\rvert - \lvert r[k+1]\rvert}{\lvert r[k-1]\rvert - 2\lvert r[k]\rvert + \lvert r[k+1]\rvert}, \qquad \hat\ell = k + \delta, \qquad \rho = \frac{r[k]}{\sqrt{E_a E_b}}$$

The correction $\delta$ is applied only when the denominator is negative and the peak is not at an edge. $\hat\ell > 0$ means $b$ is late; $\rho$ is signed, so an inverted copy gives $\rho \approx -1$.

`correlation` works over the common start of length $n = \min(N_a, N_b)$:

$$\rho_P = \frac{\sum_i (a_i - \bar a)(b_i - \bar b)}{\sqrt{\sum_i (a_i - \bar a)^2\,\sum_i (b_i - \bar b)^2}}, \qquad \cos = \frac{\sum_i a_i b_i}{\sqrt{\sum_i a_i^2\,\sum_i b_i^2}}$$

Spearman is Pearson of the ranks, ties get the average rank. All three are clamped to $[-1, 1]$ and are 0 when a variance or an energy is zero.

## Algorithm and cost

1. The correlation is a direct double sum when $N_a N_b \le 2^{16}$, otherwise a complex FFT of length $N_a + N_b - 1$ with $b$ reversed; then the requested mode is cut out.
2. `xcorr` goes over the channels of $a$, taking the channel of $b$ with the same number or the last one; different $f_s$ are refused.
3. `delay_estimate`: the peak in magnitude, the parabola, the delay in samples and seconds (needs $f_s > 0$); for silence every field is 0.
4. `correlation`: the means, ranks by a stable sort, three coefficients.

Cost of the correlation is $O(N_a N_b)$ directly or $O\big((N_a+N_b)\log(N_a+N_b)\big)$ via the FFT; `correlation` is $O(n\log n)$ because of the ranks.

## With this block's numbers

`xcorr`: mode {{block.mode}}, normalisation {{block.normalize}}.

Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- The core correlation in all three modes matches `scipy.signal.correlate(method="direct")` to $10^{-12}$ on three length pairs, a one-sample signal included; the FFT path (3000 × 700) matches the direct sum to $10^{-9}$.
- Normalised `xcorr` of a signal with itself gives 1599 values and 1 at the centre to $10^{-12}$ on both channels; `same` gives 800; different $f_s$ are refused.
- `delay_estimate` matches the numpy reference to $10^{-10}$: a 17-sample delay with noise, an inverted copy 5 samples early, a fractional delay of 3.4 samples and a truncated copy; different $f_s$, an empty channel and $f_s = 0$ are refused, silence gives delay and peak 0.
- `correlation` matches `numpy.corrcoef`, `scipy.stats.spearmanr` and the cosine to $10^{-12}$, ties and different lengths included; for a constant input Pearson and Spearman are 0, the cosine is $6/\sqrt{42}$.

## Links

- §1.1 — convolution and correlation: $r = h^{-} * x$.
- §3.9 — YIN: autocorrelation as a period search.
- The "Fundamental frequency: YIN" card — the same idea with a normalised difference function.

## Implementation

The correlation peak in magnitude and the parabolic refinement of the delay.

```cpp
                                       std::size_t k = 0;
                                       for (std::size_t i = 1; i < z.size(); ++i)
                                           if (std::abs(z[i]) > std::abs(z[k])) k = i;
                                       double delta = 0;
                                       if (k > 0 && k + 1 < z.size()) {
                                           const double y0 = std::abs(z[k - 1]), y1 = std::abs(z[k]), y2 = std::abs(z[k + 1]);
                                           if (y0 - 2 * y1 + y2 < 0) delta = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
                                       }
                                       peak_lag = static_cast<double>(k) - static_cast<double>(x.size() - 1);
                                       lag = peak_lag + delta;
```
