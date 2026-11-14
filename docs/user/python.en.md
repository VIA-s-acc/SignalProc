# Signals and Python

[Русский](python.md)

Signals exchanges `.npy` and `.npz` files with numpy, scipy and pywt. Numbers are stored as their eight double bytes, with no text in between. `np.load(path, allow_pickle=False)` opens any file the program writes, and the program opens files written by `np.save`, `np.savez` and `np.savez_compressed`.

## Export

| Where | What it does |
|---|---|
| `signals run graph.sig --npz out.npz` | every probe in one file; the key is the probe name, a dot and the value key: `clean.x`, `clean.rate` |
| `signals run graph.sig --probe-dir dir --probe-format npy` | a signal probe to `<name>.npy`, the other probes to `<name>.npz`; `wav` (the default) and `json` work as before |
| `signals dwt in.wav --wavelet db4 --levels 5 -o out.npz` | the decomposition coefficients; any other extension writes JSON as before |
| `signals filter info db4 -o bank.npz` | the filter bank of a wavelet |
| block `export(format=npy)` | the samples of a signal |
| block `export_report(format=npz)` | the fields of a report |
| Pipeline, `x` | a signal probe to `.npy`, any probe to `.npz` |
| Decomposition, `N` | the coefficients on screen |
| Filters, `N` | the bank of the selected wavelet |
| plot menu "Save the plot" with a `*.npz` path | the plot series `x0`, `y0`, `x1`, `y1`… and their `names`; a map gives `z`, `x`, `y` |

## Import

- The `wav` block and `o` on the Signal tab open `.npy` and `.npz` the way they open WAV: the format is detected from the file content.
- The rate comes from the `rate` key. An `.npy` has none: set the block's `rate` parameter; the Signal tab asks for it. A rate in the file that contradicts the `rate` parameter is an error.
- From an `.npz` the `x` array is taken. Without such a key, the only array is taken. The `key` parameter picks another one.
- An `(N,)` array is one channel, `(C, N)` is C channels. An array with more than 64 rows is an error: it is most likely `(N, C)` from soundfile, save it as `x.T`.
- The `coeffs_file` block reads coefficients from an `.npz` written by Signals or pywt. When the file has no `wavelet`, `mode` or `rate` keys, the block parameters give them. A trial inverse transform checks that the bands fit the wavelet.

## Key layout

| Value | Keys |
|---|---|
| signal | `x` — `(N,)` or `(C, N)`; `rate` |
| `dwt` and lifting coefficients | `cA{J}`, `cD{J}` … `cD1` |
| `swt` coefficients | `cA{J}`, `cD{J}` … `cD1`, as `pywt.swt(..., trim_approx=True)`; `norm` |
| `wpt` coefficients | the packet node path: `aa`, `ad`, `da`, `dd`…; `length` is required |
| `dtcwt` coefficients | `cA{J}`, `re_cD{j}` and `im_cD{j}` |
| all coefficients | `wavelet`, `mode`, `rate`, `transform`, `length`, `detail_sign` |
| report | one key per field, `passed`, `fields` — the field order |
| filter bank | `dec_lo`, `dec_hi`, `rec_lo`, `rec_hi`, `name` — as `pywt.Wavelet(...).filter_bank` |

Multichannel coefficients are `(C, len)` arrays, single-channel ones are `(len,)`.

```python
import numpy as np, pywt
f = np.load("out.npz", allow_pickle=False)
x = pywt.waverec([f["cA5"], f["cD5"], f["cD4"], f["cD3"], f["cD2"], f["cD1"]], str(f["wavelet"]), mode=str(f["mode"]))
b = np.load("bank.npz")
w = pywt.Wavelet(str(b["name"]), filter_bank=[b[k] for k in ("dec_lo", "dec_hi", "rec_lo", "rec_hi")])
```

## A script from a graph: `signals python`

```bash
signals python examples/first_steps.sig
python examples/first_steps.py
```

The command runs the graph like `signals run` does and writes a `first_steps.py` script and a `first_steps.npz` data file next to it. `-o` chooses the path, `--set node.param=value` changes parameters. A graph error gives the same text and exit code as `signals run`, and no file is written.

The script depends on numpy, scipy and PyWavelets only and follows the graph node by node: each node has its `.sig` line above it and a variable named after it. Where a block has a pywt or scipy function with the same numbers, the script calls it; the other blocks are short numpy functions at the top of the file. A signal in the script is a `(C, N)` array, coefficients are a list of bands as from `pywt.wavedec`, a report is a dict of fields.

The `.npz` holds only what Python cannot repeat:

- random samples (the noise of `gen` and `add_noise`, the dither of `quantize` and `wav_out`) before scaling, so the scaling formula and the noise spectrum shape stay in the script;
- audio scipy does not read into the same numbers: FLAC, MP3, µ-law and A-law;
- parameter lists longer than 64 numbers and the symlet filters (see below);
- the values to check against.

### The check

At the end the script compares every probe, every field of every report and the input of every node with a side effect (`wav_out`, `export`, `listen`) with what the program computed, prints a "what — max |Python − Signals| — tolerance — tolerance from" table and exits with code 1 on any ✗. A number matches when `max|a − b| ≤ tol · max(1, max|b|)`; strings are compared exactly, a file path by its file name.

By default `tol` = 10⁻¹². For a few blocks the library call gives the same numbers only more coarsely, and the script still calls it so it stays readable:

| Block | Tolerance | Call |
|---|---|---|
| `iir` | 10⁻⁸ | `scipy.signal.iirfilter`, `sosfilt`, `sosfiltfilt` |
| `remez` | 10⁻⁸ | `scipy.signal.remez` |
| `firls` | 10⁻¹⁰ | `scipy.signal.firls` |
| `lms` | 10⁻¹⁰ | own function |
| `sst_band`, `sst_ridge` | 10⁻¹⁰ | own function |
| `cepstrum` | 10⁻⁹ | own function on `numpy.fft` |

A probe takes the weakest tolerance among the blocks upstream of it; the check table names the block that set it.

Report fields in decibels (`snr_db`, `thd_db` and so on) are compared after clipping to ±200 dB: beyond that lies a ratio to double rounding noise that no other implementation repeats. So the `check` of a perfect reconstruction matches although its SNR is about 300 dB.

A wavelet goes to pywt by name when the pywt filters equal the Signals filters to 10⁻¹⁵. Twelve symlets (`sym2`–`sym10`, `sym12`, `sym13`, `sym17`) are built by Signals itself and pywt's table differs by up to 4·10⁻¹²; for them the script builds `pywt.Wavelet("sym8", filter_bank=…)` from the Signals numbers, and graphs with them stay at 10⁻¹².

Blocks that choose by comparing (peaks, onsets, the `sure` rule, the best `wpt` basis) compute the deciding value with the same operations in the same order as the program. If two values in your graph are exactly equal, the choice may differ — the check then honestly shows ✗.

The script looks for the `.npz` and the input files next to itself, so it runs from any folder and moves together with its data.

## Detail sign

Signals computes detail coefficients with the opposite sign to pywt, following Strang and Nguyen. Outside the program everything uses the pywt sign:

- writing `dwt` and `swt` flips the detail sign and stores `detail_sign = "pywt"`; the program already computes `wpt` packets in the pywt sign;
- reading a file without `detail_sign` or with `"pywt"` converts it to the Signals sign; `"signals"` is read as is;
- lifting and `dtcwt` have no pywt counterpart: their bands are written as is, with `detail_sign = "signals"`.

So `cD1` in the file is the negative of what the Decomposition tab shows. A reconstructed signal does not depend on the sign.
