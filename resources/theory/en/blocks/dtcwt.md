+++
id = "dtcwt"
title = "DTCWT — dual-tree complex DWT"
theory = ["dtcwt", "swt-modwt", "audio-subband-zeroing"]
source = "src/core/dtcwt.cpp"
function = "forward"
test = "tests/core/test_dtcwt.cpp"
views = ["under_the_hood", "decomposition"]
+++

## Purpose

Split a signal into bands whose energy barely depends on how many samples the signal is shifted by, and where editing a band barely aliases. The ordinary DWT has neither property: a shift of one sample visibly moves energy between levels. The DTCWT gets them at the price of a redundancy of two: two real DWT trees run side by side, and their outputs are the real and imaginary parts of one complex coefficient.

## Formulas

A level $j$ coefficient is a pair of outputs of trees $a$ and $b$:

$$d_j[n] = d^{a}_j[n] + i\,d^{b}_j[n]$$

At level 1 both trees use the same near-symmetric biorthogonal filters $h_0, h_1$ but decimate one sample apart: $d^{a}_1[n] = (h_1 * x)[2n]$, $d^{b}_1[n] = (h_1 * x)[2n+1]$.

From level 2 on, q-shift filters are used: tree $b$ takes the filter of tree $a$ reversed,

$$h_{0b}[n] = h_{0a}[L-1-n],$$

and $h_{0a}$ has a group delay of about $\tfrac14$ sample, $h_{0b}$ about $\tfrac34$. The trees drift half a sample apart at every level, and the wavelet of tree $b$ is close to the Hilbert transform of the wavelet of tree $a$: $\psi_b \approx \mathcal H\{\psi_a\}$. That is why $\psi_a + i\psi_b$ is nearly analytic and $|d_j[n]|$ varies smoothly and hardly depends on a shift.

Reconstruction uses the synthesis filters $g$ of the same sets; at every level the contributions of both trees are added, and the error stays at rounding level.

## Algorithm and cost

1. The signal length must be even; the block extends an odd signal by repeating its last sample and cuts it off after the inverse.
2. Level 1: convolution with $h_0$ and $h_1$ with half-sample symmetric extension at the edges (… x₁ x₀ | x₀ x₁ …), without decimation; the even and odd samples of the $h_1$ output give $\operatorname{Re}$ and $\operatorname{Im}$.
3. Levels $j \ge 2$: if the length of the lowpass branch is not a multiple of 4 it is extended by its edge samples; then one function filters and decimates both trees at once, interleaving their samples.
4. The inverse repeats the steps in reverse order and trims the extension when the length of the next band requires it.

Time is $O(N L)$, as for the DWT but twice as much: there are two trees. About $2N$ numbers are stored.

In the pipeline the band of level $j$ is stored as real $\operatorname{Re}, \operatorname{Im}$ pairs in a row, bands go from coarse to fine with the lowpass part first. This lets the coefficients pass through thresholds and MRA without loss, and `idtcwt` assembles the complex numbers again.

## With this block's numbers

Levels: {{block.levels}}; level 1 filters: {{block.biort}}; q-shift filters: {{block.qshift}}.

## Checks

- The forward and inverse transforms match the Python `dtcwt` package (Kingsbury, Wareham) to $10^{-12}$ at every level: the lowpass part and $\operatorname{Re}$ and $\operatorname{Im}$ of every band; cases cover lengths from 2 to 128, 1 to 5 levels and all filter sets.
- A random signal of 1000 samples is reconstructed to $10^{-12}$ with 6 levels and every pair of sets.
- Shifting the signal by one sample changes the DTCWT level energies by less than 2 %, and those of a db4 DWT many times more.
- An odd length, stereo and the inverse of the wrong transform are checked on the pipeline blocks.
- On "Decomposition" DTCWT is the fourth transform (`t`): the strips show the magnitude |dⱼ|, the MRA adds back to the signal, and denoising thresholds and keeping the largest coefficients act on a whole complex coefficient without changing its phase.

## Links

- §11.5 — the dual-tree complex DWT.
- §11.2 — SWT and MODWT: another way to shift invariance, at a redundancy of $J+1$.
- §12.3 — why editing DWT bands aliases and how the DTCWT helps.

## Implementation

Level 1 and the q-shift levels of the forward transform.

```cpp
    p.highpasses.push_back(to_complex(colfilter(x, biort.h1o)));
    auto lo = colfilter(x, biort.h0o);
    for (std::size_t level = 1; level < levels; ++level) {
        if (lo.size() % 4 != 0) {
            lo.insert(lo.begin(), lo.front());
            lo.push_back(lo.back());
        }
        p.highpasses.push_back(to_complex(coldfilt(lo, qshift.h1b, qshift.h1a)));
        lo = coldfilt(lo, qshift.h0b, qshift.h0a);
    }
```
