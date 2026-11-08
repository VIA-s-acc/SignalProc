+++
id = "packets"
title = "Wavelet packets and the best basis"
theory = ["wavelet-packets", "dwt-mallat", "conv-pywt", "fbm-boundary-modes"]
source = "src/core/packets.cpp"
function = "best_basis"
test = "tests/pipeline/test_blocks_more2.cpp"
views = ["decomposition"]
+++

## Purpose

Split into bands not only the low-pass branch, as the DWT does, but the details too — and pick from the full tree the partition that compresses the signal best: `wpt` builds the packet tree and outputs the coefficients of the chosen basis, `iwpt` rebuilds the signal from any admissible basis.

## Formulas

The root is $c_{0,0} = x$; every node is split by the same step as the DWT, with the pywt phase:

$$c_{j+1,2m}[k] = \sum_i h_0[i]\,c_{j,m}[2k+1-i], \qquad c_{j+1,2m+1}[k] = -\sum_i h_1[i]\,c_{j,m}[2k+1-i]$$

The minus sign is `dec_hi` from PyWavelets (§0.4), so the nodes match `pywt.WaveletPacket`. The edges follow `ext`; a child has length $\lfloor (n+L-1)/2\rfloor$, for `periodization` $\lceil n/2\rceil$.

Node $(j, m)$ is a path of $a$ and $d$: the binary digits of $m$ (Paley order). The $d$ branch flips the spectrum, so the frequency position of a node is the inverse Gray code:

$$p(m) = m \oplus \lfloor m/2\rfloor \oplus \lfloor m/4\rfloor \oplus \dots$$

The basis (`basis`): `full` is all $2^J$ leaves of level $J$ in frequency order; `dwt` is $a\dots a$ of level $J$ and the details $(J, 1), (J-1, 1), \dots, (1, 1)$; `best` is Coifman–Wickerhauser on the Shannon entropy summed over channels:

$$\mathcal{M}(c) = -\sum_k p_k \ln p_k, \qquad p_k = \frac{c_k^2}{E_x}, \quad E_x = \sum_n x[n]^2$$

Zeros are skipped; $E_x$ is the energy of the whole channel, not of the node. A node is replaced by its children when $\mathcal{M}(\text{children}) < \mathcal{M}(\text{node})$ strictly. For orthogonal banks this is the optimum over all admissible bases, for biorthogonal ones an approximation.

## Algorithm and cost

1. `wpt`: needs $N \ge 2^J$; at level $j$ each of the $2^j$ nodes is split by one DWT step, the whole tree holds at most $2^{28}$ values.
2. For `best` the costs go bottom-up, the leaves are collected by a walk from the root and sorted by frequency scaled to the deepest level.
3. `iwpt`: the basis is checked for admissibility (the leaves cover every frequency with no holes or overlaps), a tree of zeros of the same depth is built, the bands are put into it, then sibling pairs are synthesised bottom-up and cut to the parent's length.

Cost of the tree is $O(N\,L\,J)$, of the basis choice $O(N\,J)$ after the tree is built, of `iwpt` $O(N\,L\,J)$.

## With this block's numbers

Wavelet {{block.wavelet}}, levels $J$ = {{block.levels}}, basis {{block.basis}}, edge extension {{block.ext}}.

Node time {{block.wall}}, memory {{block.bytes}}.

## Checks

- The `wpt` block in five settings (db2/symmetric/best, sym4/periodization/full, haar/periodization/dwt, coif1/zero/best, stereo db2/periodization/best) matches the `pywt` packets: band paths exactly, coefficients to $10^{-10}$; `iwpt` returns the signal to $10^{-10}$.
- The sum of the `mra` components over all basis bands equals the input to $10^{-10}$; a band runs at $f_s/2^j$; `idwt` and `iswt` refuse packets, `iwpt` refuses DWT coefficients; 3 samples at 2 levels and an incomplete set of bands are refused.
- In the core: all nodes and the reconstruction match `pywt.WaveletPacket` for several wavelets, modes and lengths; any admissible basis (`dwt`, a full level, the root, 20 random ones) reconstructs the signal.
- The best basis equals the brute-force minimum over all admissible bases (2, 5, 26, 677 for $J$ = 1…4) for haar and db2, entropy and log energy, and is no worse than the root and the full level.

## Links

- §11.3 — wavelet packets and the best basis.
- §11.1 — DWT, the Mallat pyramid.
- §0.4 — high-pass signs and the PyWavelets mapping.
- §6.5 — boundary modes and output lengths.
- The "DWT — Mallat pyramid" and "IDWT — inverse pyramid" cards.

## Implementation

Coifman–Wickerhauser: a node is split only if its children together are cheaper.

```cpp
        for (std::size_t n = 0; n < count; ++n) {
            double own = 0;
            for (std::size_t t = 0; t < trees.size(); ++t) own += packet_cost(trees[t].nodes[j][n], cost, norm2[t]);
            best[j][n] = own;
            if (j == levels) continue;
            const double children = best[j + 1][2 * n] + best[j + 1][2 * n + 1];
            if (children < own) {
                best[j][n] = children;
                split[j][n] = true;
            }
        }
```
