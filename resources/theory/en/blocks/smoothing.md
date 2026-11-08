+++
id = "smoothing"
title = "Smoothing: median and Savitzky–Golay"
theory = ["cls-least-squares", "fbm-boundary-modes", "lti-convolution"]
source = "src/pipeline/blocks_design.cpp"
function = "Savgol"
test = "tests/pipeline/test_blocks_analysis.cpp"
views = ["under_the_hood"]
+++

## Purpose

Remove noise without blurring the shape of the signal: `median` replaces a sample with the median of its window and removes isolated spikes, `savgol` (Savitzky–Golay) fits a polynomial in the window by least squares and takes its value or derivative at the centre.

## Formulas

`median` with an odd window $K = 2h+1$ and zeros beyond the edges, as `scipy.signal.medfilt`:

$$y[i] = \mathrm{median}\big(x[i-h], \dots, x[i+h]\big), \qquad x[n] = 0 \text{ outside } [0, N)$$

`savgol` with an odd window $W = 2h+1$, order $p < W$ and derivative $d \le p$. The window coordinate is normalised, $u_k = (k-h)/s$, $s = \max(1, h)$, which keeps the Vandermonde matrix well conditioned up to $p = 20$:

$$\hat\beta = \arg\min_{\beta} \sum_{k=0}^{W-1} \Big(x[i-h+k] - \sum_{j=0}^{p} \beta_j u_k^j\Big)^2, \qquad y[i] = d!\,\hat\beta_d \Big(\frac{f_s}{s}\Big)^d$$

Since the problem is linear, this is a convolution with a fixed kernel: $V = QR$, $c = Q\,R^{-T}\ell$, $\ell = d!\,e_d$, and $y[i] = (f_s/s)^d \sum_k c_k\,x[i-h+k]$. The factor $(f_s/s)^d$ is the same as `delta = 1/fs` in `scipy.signal.savgol_filter`; for $d = 0$ the rate is not needed.

The `savgol` edges (`mode`): `interp` fits the polynomial to the outermost $W$ samples and evaluates it at the first and last $h$ positions; `mirror` is $x[-m] = x[m]$ without repeating the edge sample; `nearest` repeats the edge value; `constant` pads with zeros; `wrap` is periodic.

## Algorithm and cost

1. `median`: the signal is padded with $h$ zeros on each side; the sorted window is updated at every step — the leaving sample is removed and the arriving one inserted by binary search.
2. `savgol`: the matrix $V_{kj} = u_k^j$ is factored once by Householder QR; the kernel $c$ comes from $R$ and $Q$.
3. The signal is extended by `mode` and convolved with the kernel; in `interp` mode the edges are recomputed with the same QR on the end segments (needs $N \ge W$).

Cost of `median` is $O(N\,K)$ because of the shifts in the window; `savgol` is $O(N\,W)$ for the convolution (via the FFT for long windows) plus $O(W\,p^2)$ for the QR.

## With this block's numbers

`median`: window $K$ = {{block.kernel}}.

`savgol`: window $W$ = {{block.window}}, order $p$ = {{block.polyorder}}, derivative $d$ = {{block.deriv}}, edges — {{block.mode}}.

Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- `median` for windows 5 and 11 matches `scipy.signal.medfilt` exactly (difference 0); the even window 4 is refused.
- `savgol` matches `scipy.signal.savgol_filter` (with `delta = 1/fs`) in seven settings — all five edge modes, $d$ from 0 to 3, $p$ up to 5, windows up to 31 — to $10^{-10}$ of the largest $\lvert y\rvert$.
- A 7-sample signal with window 11 in the `mirror`, `nearest`, `constant` and `wrap` modes matches scipy to $10^{-12}$; in `interp` a signal shorter than the window (3 samples) is refused.
- The derivative of a ramp with slope 0.5 per second at $f_s = 1000$ equals 0.5 to $10^{-10}$ at every point, edges included; an even window, $p \ge W$, $d > p$ and a derivative without a sample rate give an error.

## Links

- §9.5 — the least-squares method.
- §6.5 — boundary modes and output lengths.
- §1.1 — convolution.
- The "Recursive filters" card — smoothing with an IIR filter via `lfilter` or `biquad`.

## Implementation

The Savitzky–Golay kernel from the QR factorisation: solve $R^{T} z = \ell$ and multiply by $Q$.

```cpp
        const auto m = static_cast<Eigen::Index>(order + 1);
        const Eigen::MatrixXd r = qr_.matrixQR().topLeftCorner(m, m).triangularView<Eigen::Upper>();
        Eigen::VectorXd z = r.transpose().triangularView<Eigen::Lower>().solve(derivative_row(0.0));
        Eigen::VectorXd padded = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(window));
        padded.head(m) = z;
        const Eigen::VectorXd h = qr_.householderQ() * padded;
```
