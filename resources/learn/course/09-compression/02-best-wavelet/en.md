# Which wavelet compresses a piecewise-constant signal best

The Donoho–Johnstone `blocks` signal: piecewise constant with jumps (`gen` with `kind=blocks`, `rate={{n}}`, `duration=1`). It is decomposed over 5 levels (`dwt`, `ext=periodization`), the 5 % of coefficients largest in modulus are kept (`keep_largest`, `mode=percent`, `amount=5`, `keep_approx=yes`) and the signal is rebuilt. Which wavelet — `haar`, `db4`, `sym8`, `bior4.4` — gives the highest SNR of the reconstructed signal?

Choose an option: its number in the list or its name.

## Hints {#hints}

1. Build four identical chains `gen → dwt → keep_largest → idwt` and put probes on the outputs. Only `wavelet` differs.
2. Compare the SNR of the outputs with the input: on the "Probe" tab or with a `check` block with `kind=snr`.
3. Think how a piecewise-constant signal resembles the Haar basis functions: they are steps of the same shape.

## Solution {#solution}

The SNR at 5 % of the coefficients: `haar` {{m.snr_h}} dB, `db4` {{m.snr_d}} dB, `sym8` {{m.snr_s}} dB, `bior4.4` {{m.snr_b}} dB. The best option: {{expected}}.

The Haar basis functions are steps. A piecewise-constant signal in this basis is written with few coefficients: only the details at the jumps are nonzero. The smooth wavelets `db4`, `sym8`, `bior4.4` give a whole "tail" of coefficients around a jump, and at the same budget some of them have to be dropped, hence the error. The suitable wavelet is determined by the shape of the signal, not by the number of vanishing moments ({#audio-compression}).
