## Appendix. The .sig pipeline format {#sig-format}

A `.sig` file is a text description of a «Pipeline» graph: which blocks to use, with which parameters and how they are connected. The «Pipeline» tab writes and reads it, `signals run` executes it and `signals graph` draws it, and it is easy to edit by hand in any editor (for VS Code there is an extension in `editors/vscode`: highlighting, hovers and completion).

A small example — a one-level Haar bank:

```
pipeline "haar_bank" v1

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

- `pipeline "name" v1` — the header: the graph name in quotes and the format version. Only `v1` exists today.
- `node id = block(param=value, …)` — a node. The id consists of Latin letters, digits and underscores and starts with a letter or `_`; ids are unique. Parameters are comma-separated in any order; omitted ones take their defaults, so `add()` and plain `add` are the same. A line may break inside the parentheses.
- `node id = block(…) bypass` — the node is bypassed: its input goes straight to its output and the block is not computed. Only blocks whose input and output have the same type can be bypassed.
- `a -> b -> c` — a chain of wires: the output of `a` into `b`, the output of `b` into `c`. A port follows a dot: `x -> pr.ref`, `sum -> pr.in`, `lms.e -> out`. A port is not allowed in the middle of a chain — split such a chain into two lines.
- `probe node as "name"` or `probe node.port as "name"` — a probe: this output is shown in the «Probe» panel. The name is optional but must not repeat.
- `#` — a comment to the end of the line (except `#` inside quotes).

### Ports {#sig-ports}

Every block has inputs and outputs of a given type (listed in the block reference and in the inspector):

- `signal` — a signal: one or more channels and a sample rate;
- `coeffs` — wavelet coefficients (from `dwt`, `swt`, lifting);
- `filter` — a filter bank;
- `scalar` — a single number;
- `report` — a report: fields and whether the check passed.

The port may be omitted when the choice is unambiguous: the node has one input or one required input, or a source has one output. With several inputs or outputs the program asks for a port and lists the options. An optional input with nothing connected receives an empty signal — for example, `multiply` without input `b` multiplies by a constant, and `spectral_subtract` without input `noise` takes the noise from the start of the signal.

A `coeffs` output has bands: `dwt.a` — the approximation of the last level, `dwt.d1` … `dwt.dJ` — the details of levels 1…J. A band can feed a signal input or carry a probe; the band number is checked against the number of levels.

### Parameter values {#sig-values}

- **Numbers:** `440`, `0.25`, `-3`, `1e-12` — with a point, not a comma.
- **Durations and samples:** parameters such as `begin`, `end`, `samples` take a number of samples or seconds with an `s` suffix: `begin=4410`, `begin=0.1s`.
- **Levels:** one number or a range `1..4`.
- **Choices:** a word from the parameter's list: `ext=symmetric`, `anti_alias=no`, `kind=pr`.
- **Filter:** `bank.role`, where the role is `h0`, `h1`, `f0` or `f1`: `haar.h0`, `db4.f1`, `sym8.h1`. A bank of your own from «My filters» is written by its name (`my_bank.h0`), a single filter of your own by its whole name.
- **Wavelet:** a library name (`db2`, `sym8`, `bior2.2`, …), your own bank by name, or `auto` — take the wavelet from the incoming coefficients.
- **Quoted strings:** `path="../audio/test.wav"`, `values="4 6 10 12"`. Quotes are needed when the value contains a space, a comma, a parenthesis, `#` or `=`. Inside quotes write `\"` and `\\`.
- **Paths:** a relative path is resolved against the folder of the `.sig` file.

Values are checked at once: a number out of range, an unknown choice or an unreadable number gives an error naming the node and the parameter.

### Graph checks {#sig-checks}

Before a run the whole graph is checked, and every error names its line:

- an unknown block or node — with a «did you mean …» hint when a similar name exists;
- two wires into one input, or a required input left unconnected;
- an output type that does not fit the input — for example `coeffs` into `signal`; the program suggests inserting `idwt` or `dwt`;
- a cycle: the graph must flow from sources to outputs without loops;
- a band `dJ` beyond the number of levels;
- one name for two probes.

### Command line {#sig-cli}

- `signals run graph.sig` — run the graph and print the stages, time and memory of every node and the check reports.
  - `--set node.param=value` — override a parameter, may repeat: `--set x.path=other.wav --set dwt.levels=5`;
  - `-o out.wav` — where to write the first `wav_out`;
  - `--probe-dir folder` — save the probes: signals as WAV, the rest as JSON;
  - `--json` — the run report as JSON.
  
  Exit code `0` — everything passed, `3` — some `check` failed, `1` — an error in the file or during the run.
- `signals graph graph.sig` — draw the graph as text (`--ascii` — ASCII only).
- `signals blocks` — the block list; `--markdown` — the `docs/user/blocks.md` reference; `--json` — block descriptions for editors.

### Where to look up blocks {#sig-blocks}

The full list of blocks with their ports, parameters, meanings and defaults is in `docs/user/blocks.md`. In the program: `a` in «Pipeline» opens the block list with descriptions, the inspector explains every parameter and value, `F1` on a node opens the block card, `O` opens the ready-made examples.
