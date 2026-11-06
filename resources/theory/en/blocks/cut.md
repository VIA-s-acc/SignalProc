+++
id = "cut"
title = "Cut a fragment"
theory = ["conv-signals", "cls-window-method"]
source = "src/core/signal_ops.cpp"
function = "cut"
test = "tests/core/test_signal_ops.cpp"
views = ["signal"]
+++

## Purpose

Keep the fragment of the signal between two samples $[b, e)$ and discard the rest. This is needed to isolate an interesting stretch for analysis, playback or saving.

## Formulas

The bounds are sample indices; the right one is not included in the fragment:

$$y[n] = x[b + n], \qquad 0 \le n < e - b, \qquad 0 \le b \le e \le N$$

The output length is $e - b$; with $b = e$ the result is an empty signal, which is allowed. The times of the bounds in seconds are $b / f_s$ and $e / f_s$, and the fragment duration is $(e - b) / f_s$.

Cutting is multiplication by a rectangular window: $y$ is $x[m]\, w[m]$ with $w[m] = 1$ on $b \le m < e$, shifted to zero. The spectrum of the fragment equals the spectrum $X$ convolved with the spectrum of the window (the Dirichlet kernel): a main lobe of width $4\pi/(e-b)$, side lobes at about $-13$ dB (§9.3). If the signal is not zero at a bound, the cut produces a jump and an audible click.

## Algorithm and cost

1. $b \le e \le N$ is checked; otherwise an error reports the allowed limit $N$.
2. The range $[b, e)$ is copied into a new vector.

Time and memory $O(e - b)$. The values do not change; the cut is exact.

## With this block's numbers

The bounds $b = {{block.begin}}$ and $e = {{block.end}}$ give the fragment $y[n] = x[{{block.begin}} + n]$ of length $e - b$ samples. If the signal is not zero at the bounds, playback may click at the edge of the fragment: a short fade removes it.

## Checks

This block has no numerical check in the interface: its correctness is pinned down by tests.

The tests check that invalid bounds give an error ($b > e$ and $e > N$), that the fragment $[1, 3)$ of $(1, 2, 3, 4)$ equals $(2, 3)$, and that the fragment $[4, 4)$ is empty.

## Links

- §9.3 — the window method: the rectangular window and its side lobes.
- §0.1 — a signal of finite length $N$ with indices $0, \dots, N-1$.
- §1.2 — the spectrum of a product with a window is the convolution of the spectra.
- The "Delay" block — the opposite action: it adds zeros on the left instead of discarding samples.

## Implementation

Bounds check and copy of the range $[b, e)$.

```cpp
Result<std::vector<double>> cut(std::span<const double> x, std::size_t begin, std::size_t end) {
    if (begin > end || end > x.size())
        return make_error(Errc::invalid_argument, "cut: need begin <= end <= " + std::to_string(x.size()));
    return std::vector<double>(x.begin() + static_cast<std::ptrdiff_t>(begin), x.begin() + static_cast<std::ptrdiff_t>(end));
}
```
