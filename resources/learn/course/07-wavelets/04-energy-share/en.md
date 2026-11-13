# Energy share of a₃

A sine from `gen` of frequency ${{f}}$ Hz, amplitude 1, sampling rate 256 Hz, duration 1 s, is mixed by an `add_noise` block with white noise: `snr_db = {{snr}}`, `seed = {{seed}}`. The signal is decomposed to 3 levels with the wavelet `{{wv}}` and periodization. What share of the total coefficient energy falls on the approximation $a_3$? The answer is a number from 0 to 1 (for example 0.25 is a quarter of the energy); tolerance 0.001.

## Hints {#hints}

1. Build this graph in the pipeline: `gen` → `add_noise` → `dwt` → `band_energy`. The numbers are the parameters from the statement.
2. The `band_energy` report gives the energy share of each band: $a_3, d_3, d_2, d_1$. You need the share of $a_3$ in channel 0; see {#audio-band-energy}.
3. The `{{wv}}` bank is orthogonal and periodization keeps the number of coefficients, so the shares of the four bands add up to 1: check it by summing all four numbers; see {#met-parseval}.

## Solution {#solution}

The share of $a_3$ is {{expected}}. Band $a_3$ covers the lowest frequencies, from 0 to $256/16 = 16$ Hz, and the ${{f}}$ Hz tone lies inside it, so almost all the energy of the tone ends up in $a_3$. The rest is noise spread over the four bands. The lower the SNR, the smaller the share of $a_3$: the noise occupies $d_1, d_2, d_3$ and partly $a_3$. The shares summing to one is Parseval's law for an orthogonal bank; see {#met-parseval} and {#audio-band-energy}.
