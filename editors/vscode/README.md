# Signals .sig and .flt for VS Code

Support for Signals pipeline files (`.sig`):

- syntax highlighting: `pipeline`, `use`, `let`, `block` with its `in` and `out` ports, `node`, `probe`, block types, parameters, values, variables `$name` and the literal `$$`, wires `a -> b.port`, paths into calls `lv1/h0`, comments `#`;
- hover over a block, a parameter or a value shows the same explanation as the Signals inspector (Russian when VS Code runs in Russian, English otherwise);
- hover over `$name` shows its `let` line, or says the variable is not declared; hover over a call of your own block shows its definition line, the comment above it and its ports;
- completion: after `node id =` your own blocks of the file and the block list (inserted with every parameter at its default), inside the parentheses the parameters (of your own blocks too), after `param=` the values with their meaning, after `$` the variables of the file;
- snippets: `pipeline`, `use`, `let`, `block`, `node`, `probe`, `wire`, `haarbank`.
- **Signals: Preview** (the button in the editor title or the command palette) runs `signals run --watch --json` on the open file and shows the run beside it: whether the checks passed, a plot of every signal probe, the coefficient bands, the reports and the state of every node. Every save reruns the graph; a graph error goes to «Problems» at its line. It needs a Signals that reads the `.sig` v2 format (1.0.0 or newer): the extension asks `signals --version --json` and names the version it found when the program is older; the `signals.path` user setting points to the program when it is not on `PATH`; a workspace cannot override it, and the program is looked up on `PATH` by an absolute path, never in the graph's folder, so a cloned folder cannot make the preview run another program. In an untrusted folder the preview does not start at all: a graph can write files.

and filter files (`.flt`), the banks and single filters of «My filters» kept in `~/.config/signals/filters/`:

- highlighting of keys, coefficient arrays, offsets and `kind`;
- hover over a key explains it; over `h0`, `f0` or `h` shows the number of taps, the gains at 0 and π and the energy, and for a bank whether it reconstructs perfectly (P₀(z) − P₀(−z) = 2z^{−ℓ}, the same check and tolerance as Signals) with the delay ℓ;
- checks while typing: syntax, unknown keys, missing `h0`/`f0` or `h`, non-integer offsets, non-numbers, and a bank that Signals would reject for lack of perfect reconstruction;
- completion of the missing keys and of `kind`, snippets `bank` and `filter`.

## Install

Copy this folder into the VS Code extensions folder and restart VS Code:

```bash
cp -R editors/vscode ~/.vscode/extensions/via-s-acc.signals-sig-0.7.0
```

or package it with `npx @vscode/vsce package` and install the `.vsix` with «Extensions: Install from VSIX…».

## Block data

`data/blocks.ru.json` and `data/blocks.en.json` are generated from the program, the Python form of each block included; a test fails when they drift:

```bash
signals --lang ru blocks --json > editors/vscode/data/blocks.ru.json
signals --lang en blocks --json > editors/vscode/data/blocks.en.json
```

## Tests

The preview code has no VS Code dependency and is tested with Node:

```bash
node --test editors/vscode/test/preview.test.js
```
