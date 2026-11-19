## Appendix. The .sig pipeline format {#sig-format}

A `.sig` file is a text description of a «Pipeline» graph: which blocks to use, with which parameters and how they are connected. The «Pipeline» tab writes and reads it, `signals run` executes it and `signals graph` draws it, and it can be edited by hand in any text editor (for VS Code there is an extension in `editors/vscode`: highlighting, hovers and completion).

A small example — a one-level Haar bank:

```
pipeline "haar_bank" v2

# an eight-sample signal, as in «Explain»
node x   = samples(values="4 6 10 12 8 6 5 5", rate=8)
node h0  = convolve(filter=haar.h0, method=direct)
node h1  = convolve(filter=haar.h1, method=direct)
node a   = downsample(m=2, anti_alias=no, phase=1)
node d   = downsample(m=2, anti_alias=no, phase=1)
node ua  = upsample(m=2, anti_image=no)
node ud  = upsample(m=2, anti_image=no)
node f0  = convolve(filter=haar.f0, method=direct)
node f1  = convolve(filter=haar.f1, method=direct)
node sum = add()
node pr  = check(kind=pr, tol=1e-12, delay=auto)

x -> h0 -> a -> ua -> f0 -> sum.a
x -> h1 -> d -> ud -> f1 -> sum.b
x -> pr.ref
sum -> pr.in
probe a as "a"
probe d as "d"
probe sum as "x̂"
```

### Lines {#sig-lines}

One statement per line. The order of lines does not matter: a wire may come before the nodes it connects. Blank lines and indentation are allowed.

- `pipeline "name" v2` — the header: the graph name in quotes and the format version. The current version is `v2`. The program reads `v1` files itself and migrates them to `v2` when it opens them (section «Format versions»).
- `let name = value` — a variable (section «Variables»).
- `node id = block(param=value, …)` — a node. The id consists of Latin letters, digits and underscores and starts with a letter or `_`; ids are unique. Parameters are comma-separated in any order; omitted ones take their defaults, so `add()` and plain `add` are the same. A line may break inside the parentheses.
- `node id = block(…) bypass` — the node is bypassed: its input goes straight to its output and the block is not computed. Only blocks whose input and output have the same type can be bypassed.
- `a -> b -> c` — a chain of wires: the output of `a` into `b`, the output of `b` into `c`. A port follows a dot: `x -> pr.ref`, `sum -> pr.in`, `lms.e -> out`. A port is not allowed in the middle of a chain; split such a chain into two lines.
- `probe node as "name"` or `probe node.port as "name"` — a probe: this output is shown in the «Probe» panel. The name is optional but must not repeat.
- `#` — a comment to the end of the line (except `#` inside quotes). Comments survive saving from the «Pipeline» tab: comment lines above a node, wire or probe stay above it, a comment at the end of a line stays at its end, and blank lines between groups of nodes are kept too. A comment inside the parentheses of a multi-line node moves to the line above the node. A deleted node takes its comments with it; if the first wire of a chain is deleted, the chain's comment moves to the remaining wire of that line.

### Ports {#sig-ports}

Every block has inputs and outputs of a given type (listed in the block reference and in the inspector):

- `signal` — a signal: one or more channels and a sample rate;
- `coeffs` — wavelet coefficients (from `dwt`, `swt`, `wpt`, `dtcwt`, lifting);
- `filter` — a filter bank;
- `scalar` — a single number;
- `report` — a report: fields and whether the check passed.

The port may be omitted when the choice is unambiguous: the node has one input or one required input, or a source has one output. With several inputs or outputs the program asks for a port and lists the options. An optional input with nothing connected receives an empty signal: for example, `multiply` without input `b` multiplies by a constant, and `spectral_subtract` without input `noise` takes the noise from the start of the signal.

A `coeffs` output has bands: `dwt.a` — the approximation of the last level, `dwt.d1` … `dwt.dJ` — the details of levels 1…J. A band can feed a signal input or carry a probe. The band number is checked in advance for a `dwt` with a set number of levels; `wpt` packet bands are named by node paths (`aa`, `ad`, …) and are chosen in `mra.component` and `band_edit.enabled`.

### Parameter values {#sig-values}

- **Numbers:** `440`, `0.25`, `-3`, `1e-12` — with a point, not a comma.
- **Expressions instead of numbers:** a numeric parameter may be an expression in quotes: `db="20*log10(2)"`, `m="2^3"`, `cutoff="44100/4"`; a bare name such as `fs/4` does not work, but `cutoff="$fs/4"` with a variable `fs` does. It supports `+ - * / ^`, parentheses, `abs min max sqrt log10` and `pi`. The file keeps the text of the expression and the run uses its value; an integer parameter needs a whole value.
- **Durations and samples:** some parameters take a number of samples or seconds with an `s` suffix — `cut.begin`/`end`, `delay.samples`, `pad.before`/`after`, `fade.fade_in`/`fade_out`, `sample_hold.hold`, `impulse_train.period`/`offset` and others (the inspector shows where): `begin=4410`, `begin=0.1s`. Other integer parameters, such as `shift.samples`, take samples only.
- **Levels:** one number or a range `1..4`.
- **Choices:** a word from the parameter's list: `ext=symmetric`, `anti_alias=no`, `kind=pr`.
- **Filter:** `bank.role`, where the role is `h0`, `h1`, `f0` or `f1`: `haar.h0`, `db4.f1`, `sym8.h1`. A bank of your own from «My filters» is written by its name (`my_bank.h0`), a single filter of your own by its whole name.
- **Wavelet:** a library name (`db2`, `sym8`, `bior2.2`, …), your own bank by name, or `auto` — take the wavelet from the incoming coefficients.
- **Quoted strings:** `path="../audio/test.wav"`, `values="4 6 10 12"`. Quotes are needed when the value contains a space, a comma, a parenthesis, `#` or `=`. Inside quotes write `\"` and `\\`.
- **Paths:** a relative path is resolved against the folder of the `.sig` file.

Values are checked at once: a number out of range, an unknown choice or an unreadable number gives an error naming the node and the parameter.

### Variables {#sig-vars}

A variable gives several nodes one value: `let name = value`, and `$name` in a parameter.

```sig
let fs = 8000
let levels = 3
let quiet = "-6*2"
node x = gen(kind=sine, freq="$fs/16", rate=$fs, duration=0.5)
node g = gain(db=$quiet)
node d = dwt(wavelet=db4, levels=$levels)
x -> g -> d
probe d.a as "a"
```

- **Name and value.** A name is Latin letters, digits and `_`, starting with a letter or `_`; a name is declared once. The value is written like a parameter value: a number, a word or text in quotes. A `let` line may stand anywhere in the file.
- **Variables that use variables.** A variable may use others (`let half = "$fs/2"`) in any line order, but not in a circle: the program names the whole circle, for example `a → b → a`.
- **Numbers.** In a numeric parameter a variable is part of an expression: `freq="$fs/16"`. An expression value goes in parentheses: with `let a = "1+1"`, `"$a*2"` gives 4, not 3.
- **Choices, ranges, samples, sigmas** take only a whole variable: `wavelet=$w` works, `wavelet="$w.5"` does not.
- **Text and paths.** Substitution is textual: `path="$dir/a.wav"`.
- **The `$` sign.** A literal `$` is written `$$`: `path="cost$$1.wav"` reads the file `cost$1.wav`.
- **Errors.** An unknown variable is an error that suggests the nearest name. An error names the `let` line or the node and parameter.
- **Limits.** Nesting is at most 64 levels deep, one value is at most 65,536 characters after substitution, and the whole graph at most 8 MiB of substitutions. So a file whose variables use each other many times over cannot hang the program.

Where else variables show:

- **Command line.** `--set name=value` (no dot) changes a variable, and `signals sweep … --vary name=values` sweeps it; every node that uses it changes together.
- **«Pipeline».** The «Variables» panel (reach it with `Tab`): `↑↓` choose, `Enter` edits the value, `+` adds one (`name = value`), `Del` removes it, `S` sweeps it. The program does not remove a variable in use and names its nodes. Nodes that use the chosen variable are highlighted in the graph. The inspector shows such a parameter as `$lv → 2`; the `←→` arrows leave it alone and `Enter` edits the text.
- **Python.** `signals python` writes the variables at the top of the script as Python variables (`lv = 3`), and the notebook in the «Graph parameters» cell; parameters given by a variable refer to it (`level=lv`). Change a value and the script skips the check against the program and computes with the new values.
- **VS Code.** The extension highlights `let` and `$name`, offers the file's variables after `$` and shows the `let` line on hover.

### Your own blocks {#sig-own-blocks}

A part of the graph that repeats can be described once as your own block and called like a built-in one.

```sig
# One level of a bank: analysis, synthesis and the sum
block bank_level(wavelet=haar) {
    in x
    out y, a, d

    node h0  = convolve(filter=$wavelet.h0, method=direct)
    node h1  = convolve(filter=$wavelet.h1, method=direct)
    node da  = downsample(m=2, anti_alias=no, phase=1)
    node dd  = downsample(m=2, anti_alias=no, phase=1)
    node ua  = upsample(m=2, anti_image=no)
    node ud  = upsample(m=2, anti_image=no)
    node f0  = convolve(filter=$wavelet.f0, method=direct)
    node f1  = convolve(filter=$wavelet.f1, method=direct)
    node sum = add()

    x -> h0 -> da -> ua -> f0 -> sum.a
    x -> h1 -> dd -> ud -> f1 -> sum.b
    da -> a
    dd -> d
    sum -> y
    probe sum as "x̂"
}

node x   = samples(values="4 6 10 12 8 6 5 5", rate=8)
node lv1 = bank_level()
node lv2 = bank_level(wavelet=db2)
x -> lv1.x
lv1.a -> lv2.x
probe lv2/h0 as "h0 of the second level"
```

- **Definition.** `block name(param=value, …) { … }`: the parentheses hold the block's params with their defaults; inside go ports, nodes, wires, probes and `let`. The comment above the `block` line describes the block: `signals explain` and the block card show it.
- **Params.** Inside the block a param is a variable: `filter=$wavelet.h0`. The graph's variables are not visible inside, so a value is passed through the call: `bank_level(wavelet=$w)`.
- **Ports.** `in x` is an input, `in noise optional` an optional input, `out y, a` the outputs. Every input is wired to something inside, every output gets exactly one wire. In a call every input except the optional ones must be wired. A port has the type of the node behind it: a signal or coefficients.
- **Call.** `node lv1 = bank_level(wavelet=db2)`; the call's ports are written with a dot: `lv1.x`, `lv1.a`. A single port may go unnamed. A call with one input and one output can be bypassed: `bypass`.
- **Nodes inside a call** are named with a slash: `lv1/h0`, `lv2/sum`. That is how a probe is put on them from outside: `probe lv2/h0 as "…"`; a wire does not go to or from a node inside a call — the value comes out through an `out` port. A probe inside the definition gives a probe in every call: `lv1/x̂`, `lv2/x̂`.
- **Nesting.** A block may call your other blocks, at most 16 levels deep; the whole unfolded graph has at most 100,000 nodes, wires and probes. A block that calls itself, directly or through others, is an error that names the whole chain.
- **Errors inside a block** name the call, the definition and its line: "in lv2 (bank_level, line 5): …".
- **Libraries.** `use "lib/banks.sig"` takes definitions from another file; the path is relative to the graph's folder. A library file holds only the header, definitions and comments. A library used twice (directly and through another library) is read once. One name in the graph and in a library is an error; your block with a built-in block's name hides the built-in one, and the program warns about it. Paths inside a block (`path=` of `wav`, `coeffs_file`) are relative to the folder of the file where the block is defined.
- **Your library** is the `*.sig` files in the `blocks` folder next to the settings (`~/.config/signals/blocks`). `signals explain name` finds a block there.

Where your blocks show up too:

- **«Pipeline».** `m` marks nodes, `b` collapses the marked ones into a block: the program asks for a name, turns the wires across the border into `in` and `out` ports and the variables into block params, and puts a call in place of the nodes; the numbers stay the same. The graph shows a call as one node and the run panel its total time. `i` on a call enters the block: the graph panel shows the definition's body with its ports, the title shows the path "Graph ▸ lv2 · bank_level", and with several calls the warning "changes affect 2 calls". An edit inside changes the definition, a probe inside becomes a probe of this call (`probe lv2/h0`), the run panel shows the times of the inner nodes. `Esc` leaves the block, `u` on a call unfolds it back into nodes.
- **Command line.** `--set lv1.wavelet=db4` changes a call's param; a node inside a call is not changed this way (`--set lv1/h0.method=fft` is an error with a hint). `signals graph` draws a call as one node, `--expand` with all the nodes inside. `signals run` prints the call's line with the sum of time and memory and the call's nodes under it; with `--json` such nodes get an `instance` field. `signals explain name --graph graph.sig` shows a block from the graph file and its libraries: the description, params, ports with types and the definition. `--watch` follows the `use` files too.
- **Probe file names.** In `--probe-dir`, `--npz` and `--report` the signs `/ \ : * ? " < > |` in a probe name become `_`, and names that collide get `-2`, `-3`.
- **Python.** `signals python` writes each definition as one Python function, and the calls call it with their own arguments (the "Your own blocks" section of the Python guide).
- **Course tasks** see the nodes inside your blocks (the task checks section of the course guide).
- **VS Code.** The extension highlights `block`, `use`, `in`, `out` and paths like `lv1/h0`, inserts a template with the `block` snippet, completes your blocks' names and params and shows their description and ports on hover.

### Graph checks {#sig-checks}

Besides `check`, the `assert` block sets conditions on the results of a run. Its inputs `a`, `b`, `c`, `d` take the reports of other blocks, and the parameter `expr` is an expression over their fields:

```
node c = check(kind=snr, tol=0)
node g = assert(expr="a.snr_db >= 30 && a.passed")
c -> g.a
```

Field names are those of the block's report: `a.max_abs`, `a.snr_db`, `b.lag_samples`; `a.passed` is 1 if the check passed. A false expression marks the node as failed and, like `check`, gives exit code 3 from `signals run`. A typo in a field name is a node error that suggests the closest name.


Before a run the whole graph is checked, and every error names its line:

- an unknown block or node — with a «did you mean …» hint when a similar name exists;
- two wires into one input, or a required input left unconnected;
- an output type that does not fit the input (for example `coeffs` into `signal`); the program suggests inserting `idwt` or `dwt`;
- a cycle: the graph must flow from sources to outputs without loops;
- a band `dJ` beyond the number of levels;
- one name for two probes.

### Command line {#sig-cli}

- `signals run graph.sig` — run the graph and print the stages, time and memory of every node and the check reports.
  - `--set node.param=value` — override a parameter, may repeat: `--set x.path=other.wav --set dwt.levels=5`; `--set name=value` without a dot changes a variable;
  - `-o out.wav` — where to write the first `wav_out`;
  - `--probe-dir folder` — save the probes: signals as WAV, the rest as JSON;
  - `--values N` — print the first N samples of every signal probe (`0` for none); without it, all samples of signals up to 64 long are printed;
  - `--watch` — after the run, watch the graph file and its input files (the `wav` block) and run again on every change; output files are not watched, so a run does not restart itself. `Ctrl+C` exits with code 0; with `--json` every run is one JSON line on stdout (NDJSON), a graph error is a line `{"error": {"file", "line", "message"}}`, and the status lines go to stderr;
  - `--report report.md` — a Markdown report: the graph, the node table, plots of the probes in time and spectrum (SVG and CSV in the `report_files` folder), the report fields;
  - `--json` — the run report as JSON.
  
  Exit code `0` — everything passed, `3` — some `check` or `assert` failed, `1` — an error in the file or during the run, `130` — the run was interrupted with Ctrl+C.
- `signals sweep graph.sig --vary node.param=values --measure measure` — a parameter sweep: the graph runs once for every value of the parameter, and each run prints a row of measures. A variable can be swept instead of `node.param`: `--vary levels=1:5:1`.
  - Values: `0:1:0.1` (from, to, step), `1:100@5` (5 evenly spaced points), `1:100@5log` (5 points on a log scale), `haar,db4,sym8` (a list, which also works for choices). More than 1000 values are rejected.
  - Measure: a report field `node.field` (for example `chk.snr_db`) or a measure of a node's output `node:rms`, `node:peak`, `node:energy`, `node:mean`, `node:length`. `--measure` may repeat.
  - `--set` as in `run`; `--csv table.csv` and `--svg plot.svg` save the result, `--json` prints it as JSON.
  - Blocks that write files (`wav_out`, `export`) do not run in a sweep unless they are measured, so a sweep does not overwrite the results of a normal run. A failed run does not stop the sweep: its row keeps empty measures and the error text.

  For example, which wavelet removes the noise in `examples/denoise.sig` best:

  ```
  $ signals sweep examples/denoise.sig --vary "dwt.wavelet=haar,db4,sym8,coif3" --measure chk.snr_db
  sweep: dwt.wavelet, 4 runs
    dwt.wavelet  chk.snr_db
    haar         27.9459
    db4          46.3981
    sym8         51.2006
    coif3        49.7729
  ```

  In the «Pipeline» tab `S` on the selected node does the same: it asks for the parameter, the values (it suggests a range around the current one) and the measure, and the «sweep» probe shows the result as a plot or a table (`v`). `Esc` stops the sweep.
- `signals graph graph.sig` — draw the graph as text (`--ascii` — ASCII only, `--expand` — show the nodes inside calls of your blocks).
- `signals blocks` — the block list; `--markdown` — the `docs/user/blocks.md` reference; `--json` — block descriptions for editors.
- `signals explain block` — one block in detail: what it does, its ports, every parameter with its default, range and explanation, the meaning of every choice and the theory card. A typo in the name suggests the nearest block.

### Format versions {#sig-versions}

The number in the header is the version of the format, not of the program. The current version is `v2`. Signals reads files of every earlier version and writes only the current one.

- **Opening an old file.** The «Pipeline» tab, `signals run`, `watch`, `sweep`, `graph`, `python`, course tasks and students' work migrate an old file in memory. The file on disk does not change. The «Pipeline» tab says «graph v1 migrated to v2…» in its notice line, and the palette command «What changed in the migration» lists the changes. `Ctrl+S` saves the graph as `v2`. The CLI prints one line about the migration on stderr, and `--json` and the NDJSON of `watch` gain the fields `migrated_from` and `migration_notes`.
- **Migrating files on disk** — `signals migrate`:
  - `signals migrate PATH…` — files and folders (folders are searched recursively). Prints the version of every file and what would change, changing nothing.
  - `--write` — rewrite old files. A copy `name.v1.sig` is kept next to each; `--no-backup` skips it.
  - `--check` — exit code 1 if any file is in an old format: CI uses it to check that a repository is migrated.
  
  In a course folder the command also migrates `task.toml`: the names of blocks, parameters and fields (the copy is `task.v1.toml`). A file that does not parse is reported with its line, and the other files are still migrated. The command skips `*.v1.sig` and `*.v1.toml` copies, and an own course does not count them as tasks.
- **What the `v1` → `v2` migration changes.** In `v2` the `$` sign starts a variable, so a literal `$` in a parameter value is written as `$$`. The migration does it: `path="a$b.wav"` becomes `path="a$$b.wav"`, and the program still reads the file `a$b.wav`. The migration also replaces old names of parameters, choices and report fields with the new ones — they are listed in section «What changed in v2».
- **Encoding and line endings.** A file with a UTF-8 byte order mark (BOM) and with Windows line endings (CRLF) reads like any other. The program writes UTF-8 without a BOM and LF line endings.
- **A file newer than the program.** This program does not open a `v3` file and asks you to update Signals.

The rule for the future: every change of the format is a new version and a migration step. Old versions are always read, and samples of each are kept in the tests (`tests/fixtures/sig/v1`): the migration must give the same text and the same numbers.

### What changed in v2 {#sig-v2-changes}

Before the format is frozen, the names of blocks, parameters, choices, ports and report fields were checked against one rule: one meaning, one name in every block. Only names that called the same thing differently or misled were changed. `v1` files with the old names migrate by themselves: in nodes, in `assert(expr=…)`, in course `task.toml`; `--set` and `--measure` with an old name give the new one.

| Block | Was | Now | Why |
|---|---|---|---|
| `speed` | `rate` | `factor` | `rate` is the sample rate everywhere else; here it says how many times faster |
| `time_stretch` | `rate` | `factor` | `rate` is the sample rate everywhere else; here it says how many times faster |
| `lms` | `taps` | `numtaps` | the number of taps is `numtaps` everywhere; `taps` of `fir_taps` are the taps themselves |
| `pitch` | `fmin` | `min_hz` | frequency bounds in hertz are `min_hz` and `max_hz` everywhere |
| `pitch` | `fmax` | `max_hz` | frequency bounds in hertz are `min_hz` and `max_hz` everywhere |
| `sst_band` | `lo` | `min_hz` | frequency bounds in hertz are `min_hz` and `max_hz` everywhere |
| `sst_band` | `hi` | `max_hz` | frequency bounds in hertz are `min_hz` and `max_hz` everywhere |
| `sst_ridge` | `lo` | `min_hz` | frequency bounds in hertz are `min_hz` and `max_hz` everywhere |
| `sst_ridge` | `hi` | `max_hz` | frequency bounds in hertz are `min_hz` and `max_hz` everywhere |
| `loudness` | `window` | `frame` | the analysis frame length with a `hop` is `frame` everywhere |
| `spectral_subtract` | `n` | `frame` | the analysis frame length with a `hop` is `frame` everywhere |
| `onsets` | `n` | `frame` | the analysis frame length with a `hop` is `frame` everywhere |
| `time_stretch` | `n_fft` | `frame` | the analysis frame length with a `hop` is `frame` everywhere |
| `pitch_shift` | `n_fft` | `frame` | the analysis frame length with a `hop` is `frame` everywhere |
| `savgol` | `window` | `length` | the smoothing window length is `length` everywhere; `window` of `fir` is the window shape |
| `median` | `kernel` | `length` | the smoothing window length is `length` everywhere; `window` of `fir` is the window shape |
| `wiener` | `size` | `length` | the smoothing window length is `length` everywhere; `window` of `fir` is the window shape |
| `moving_average` | `centred` | `compensate` | removing the filter delay is `compensate` everywhere |
| `multiply` | `factor` | `scale` | the constant factor of `affine` is `scale` |
| `dc_offset` | `value` | `offset` | the added constant of `affine` is `offset` |
| `impulse_train` | `offset` | `begin` | a start position is `begin` everywhere; `offset` is an added constant |
| `wav_out` | `dither=tpdf` | `dither=yes` | `quantize` has the same choice as `yes` or `no` |
| `wav_out` | `dither=none` | `dither=no` | `quantize` has the same choice as `yes` or `no` |
| `loudness` | field `windows` | field `frames` | the number of analysis frames is `frames` everywhere; `assert` and `--measure` migrate with the graph |

### Where to look up blocks {#sig-blocks}

The full list of blocks with their ports, parameters, meanings and defaults is in `docs/user/blocks.md`. In the program: `a` in «Pipeline» opens the block list with descriptions, the inspector explains every parameter and value, `F1` on a node opens the block card, `O` opens the ready-made examples.
