# Keep few coefficients

The signal `x` is `gen` with `kind=doppler`, `rate=1024`, `duration=1`. Build compression: `dwt` → `keep_largest` (node `keep`) → `idwt` → `y` (the output) and keep at most $p = {{p}}$ % of nonzero coefficients so that the $\mathrm{SNR}$ of the output $y$ relative to `x` is at least 30 dB. The wavelet, the number of levels and the parameters of `keep_largest` are your choice. Do not change node `x`. The start graph keeps all the coefficients and uses Haar.

The share `kept` in the check is the share of nonzero coefficients at the output of node `keep` among all of them, the approximation $a_J$ included.

## Goals {#goals}

1. At most {{p}} % of the coefficients in `keep` are nonzero.
2. $\mathrm{SNR}(y, x) \ge 30$ dB.
3. Every path from `x` to `y` goes through `dwt`.
4. Every path from `x` to `y` goes through `idwt`.

## Hints {#hints}

1. Set `amount` of `keep_largest` to the allowed share: every percent above it already breaks goal 1. With `keep_approx=yes` the approximation counts in `kept` too.
2. Haar does not give 30 dB on an oscillation with a growing period at this budget: its basis functions are steps. Try a smooth wavelet, for example `db4` or `sym8`.
3. If the SNR falls short, change `levels`: fewer levels make the approximation longer and take more of the budget, more levels make it shorter. Pick it so that the SNR stays above 30 dB at the allowed `amount`.

## Solution {#solution}

On the reference solution (`db4`, 5 levels, `amount=6`) the share of nonzero coefficients is {{m.kept}}, the SNR is {{m.snr}} dB.

Compression works as long as the energy sits in a few coefficients. For smooth and quasi-harmonic signals a wavelet with several vanishing moments packs the energy into a small share of the coefficients, while Haar, with its steps, does worse. The largest coefficients carry almost all the energy, the rest are zeroed and give the error ({#audio-compression}, {#transforms}).
