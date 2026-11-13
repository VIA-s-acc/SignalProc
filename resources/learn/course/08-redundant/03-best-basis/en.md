# Leaves of the best basis

The signal: `gen` with `kind=heavisine`, `rate=256`, `duration=1`. Decompose it with wavelet packets: `wpt` with `wavelet=db4`, `levels=4`, `basis=best`, `ext=periodization`. How many bands (tree leaves) does the chosen best basis have?

The answer is an integer.

## Hints {#hints}

1. On the "Pipeline" tab build the chain `gen → wpt`, add `coeff_stats` and run. The report lists the bands by the names of their paths (`aaad`, `ad`, …).
2. Each band has three fields in the report: energy, entropy and the share of zeros. The number of bands is the number of distinct names.
3. A check: the energy shares of the bands add up to 1, and the full 16 equal bands of `basis=full` are not what is asked: the best basis merges neighbouring nodes if that lowers the entropy.

## Solution {#solution}

The answer: {{expected}} bands.

The Coifman–Wickerhauser best basis walks the tree bottom-up: a node is replaced by its children if their total cost (entropy) is lower than the cost of the node. For a smooth signal almost all the energy lies in the lowest band, and the tree splits deepest there, while the upper branches stay coarse. So there are fewer than 16 leaves, and the partition is uneven ({#wavelet-packets}).
