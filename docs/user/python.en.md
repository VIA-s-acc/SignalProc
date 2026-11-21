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
- A compressed `.npz` member that expands to more than 256 MiB and more than 200 times is not read: that is what a zip bomb looks like. Save a large array of zeros with `np.savez`, uncompressed.
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

The command runs the graph like `signals run` does and writes a `first_steps.py` script and a `first_steps.npz` data file next to it. `-o` chooses the path, `--set node.param=value` changes parameters. A graph error gives the same text and exit code as `signals run`, and no file is written. The script name cannot be the name of a Python module (`math.py`, `signal.py`, `numpy.py`): from the same folder Python would load the script instead of the module, so the command refuses such a name — choose another with `-o`.

The script depends on numpy, scipy and PyWavelets only and follows the graph node by node: each node has its `.sig` line above it and a variable named after it. The graph variables (`let`) stand at the top of the script as Python variables (`fs = 8000`, `half = fs / 2`, in the order they depend on each other), and in the notebook in the «Graph parameters» cell. A name taken in Python gets a trailing `_`: `let np = 3` becomes `np_ = 3`. A parameter given by a variable refers to it: `level=lv`. The script remembers the values it was built from (`GRAPH`). Change them and it prints «The parameters changed: the check against the program is skipped…», computes with the new values and exits with code 0. Where a block has a pywt or scipy function with the same numbers, the script calls it; the other blocks are short numpy functions at the top of the file. A signal in the script is a `(C, N)` array, coefficients are a list of bands as from `pywt.wavedec`, a report is a dict of fields.

The `.npz` holds only what Python cannot repeat:

- audio scipy does not read into the same numbers: FLAC, MP3, µ-law and A-law;
- parameter lists longer than 64 numbers and the filters of wavelets where pywt differs from Signals: the symlets (see below) and your own banks;
- the values to check against.

The script draws the noise (`gen`, `add_noise`) and the dither (`quantize`, `wav_out`) itself with the generator the program uses — `mt19937_64` and the polar method rewritten in numpy give the same numbers bit for bit.

### A live script

The script does not replay the run as numbers; it computes it again: every node parameter and every graph variable is a Python value, and everything derived from them is computed in the script.

- **Parameters are Python values.** A block form writes a parameter as a literal (`0.5`), a variable (`fs`) or an expression (`fs / 8`). Lengths are computed in the script (`samples_count(0.25, 8000)`), seconds become samples at the input rate (`as_samples("0.1s", x_fs)`), scipy designs the filters, numpy builds the windows and frequency grids.
- **Rates and coefficient details.** Every signal has its rate `x_fs`, coefficients have a `c_meta` dict: wavelet, extension mode, length, rate. The inverse transform, the threshold and the band tools take them from there, so a new rate or a new wavelet upstream reaches every node downstream.
- **A wavelet from a variable** is written `wavelet(name)`. The function takes the Signals filters from the `.npz` when the graph had that wavelet or it is a symlet, `pywt.Wavelet(name)` otherwise, and stops with a clear error for a name found nowhere.
- **What stays data.** Only external files: audio scipy does not read itself, `.npy`/`.npz` arrays and the filters of «My filters». To take another file, replace the path or the array.

Change a graph variable at the top of the script and it recomputes everything that depends on it and skips the check (see `GRAPH` above). A number in a node line can be edited too, and everything downstream is recomputed, but then the check shows the difference from the program: it compares with the run of the graph. CI checks this: for every parameter of every block the test makes the parameter a variable, puts a second value into the script and compares the result with `signals run --set` at that value. Eight parameters cannot be checked this way; they are listed with their reasons in `tests/python/live_exceptions.txt`: `idwt.wavelet` and `iswt.wavelet` only check the coefficients, `idwt.ext` matters only for `periodization`, `mid_side` encodes and decodes with the same matrix, `wav_out.bits`, `dither` and `seed` change only the written file, and the `assert.expr` condition is translated into a Python code line when the script is made — change it there.

In the Pipeline tab `P` or the palette item "Pipeline: export to Python…" does the same: a `.py` path gives a script, an `.ipynb` path a notebook. The graph open in the editor is exported, unsaved edits included. The inspector shows an "In Python" line for the selected node, and `signals explain <block>` shows the Python form of the block with the default parameters.

### A Jupyter notebook

```bash
signals python examples/first_steps.sig -o first_steps.ipynb
```

With an `.ipynb` path the command writes a Jupyter notebook instead of a script. Its code is the code of the script, one cell per node. Above each cell is the name of the block and what it does; after each signal probe comes its plot. The `listen` block becomes an `IPython.display.Audio` player in the notebook. The last cell checks the numbers against the program and fails on `assert` if any row does not match. The notebook looks for the `.npz` and the input files in the current folder, so start Jupyter from the notebook's folder. It also needs matplotlib and IPython; the versions the notebooks are checked on in CI are in `tools/requirements-bridge.txt`.

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
| `time_stretch`, `pitch_shift` | 10⁻⁹ | own function on `numpy.fft` |
| `inst_freq` | 10⁻⁷ | `scipy.signal.hilbert` |

The phase vocoder of `time_stretch` and `pitch_shift` adds up the phase of every frame, so the last bit of the FFT grows with the length of the audio. And numpy's last bit depends on the build: the wheels for Apple Silicon compute the FFT with fused multiply-add (FMA), Signals without it. Ten seconds of audio give 5·10⁻¹²; the 10⁻⁹ tolerance covers about half an hour.

`inst_freq` has the same issue for another reason: the difference of neighbouring phases is taken after unwrapping, the unwrapped phase grows with the length of the signal, and so does its last bit. Ten seconds of 44.1 kHz audio give 2.6·10⁻⁸ Hz; the 10⁻⁷ tolerance covers several hours.

A probe takes the weakest tolerance among the blocks upstream of it; the check table names the block that set it.

Report fields in decibels (`snr_db`, `thd_db` and so on) are compared after clipping to ±200 dB: beyond that lies a ratio to double rounding noise that no other implementation repeats. So the `check` of a perfect reconstruction matches although its SNR is about 300 dB.

A wavelet goes to pywt by name when the pywt filters equal the Signals filters to 10⁻¹⁵. Twelve symlets (`sym2`–`sym10`, `sym12`, `sym13`, `sym17`) are built by Signals itself and pywt's table differs by up to 4·10⁻¹²; for them the script builds `pywt.Wavelet("sym8", filter_bank=…)` from the Signals numbers, and graphs with them stay at 10⁻¹².

Blocks that choose by comparing (peaks, onsets, the `sure` rule, the best `wpt` basis) compute the deciding value with the same operations in the same order as the program. If two values in your graph are exactly equal, the choice may differ — the check then honestly shows ✗.

The `bytes` field of `export` and `export_report` in CSV and JSON is checked to 10⁻²: numbers are written with 17 significant digits, and a number that matches only to the tolerance may take one character more or less.

The script looks for the `.npz` and the input files next to itself, so it runs from any folder and moves together with its data.

## Preview in VS Code

The VS Code extension in `editors/vscode` shows the graph beside the file: the "Signals: Preview" command (a button in the editor title) runs `signals run --watch --json` and on every save draws the signal probes, the coefficient bands, the reports and the state of the nodes. A graph error goes to "Problems" at its line. It needs Signals 0.9.0 or newer; the `signals.path` setting gives the path to the program. The hover over a block shows its Python form.

## Detail sign

Signals computes detail coefficients with the opposite sign to pywt, following Strang and Nguyen. Outside the program everything uses the pywt sign:

- writing `dwt` and `swt` flips the detail sign and stores `detail_sign = "pywt"`; the program already computes `wpt` packets in the pywt sign;
- reading a file without `detail_sign` or with `"pywt"` converts it to the Signals sign; `"signals"` is read as is;
- lifting and `dtcwt` have no pywt counterpart: their bands are written as is, with `detail_sign = "signals"`.

So `cD1` in the file is the negative of what the Decomposition tab shows. A reconstructed signal does not depend on the sign.
