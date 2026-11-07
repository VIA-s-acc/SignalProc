# Signals .sig for VS Code

Support for Signals pipeline files (`.sig`):

- syntax highlighting: `pipeline`, `node`, `probe`, block types, parameters, values, wires `a -> b.port`, comments `#`;
- hover over a block, a parameter or a value shows the same explanation as the Signals inspector (Russian when VS Code runs in Russian, English otherwise);
- completion: after `node id =` the block list (inserted with every parameter at its default), inside the parentheses the parameters, after `param=` the values with their meaning;
- snippets: `pipeline`, `node`, `probe`, `wire`, `haarbank`.

## Install

Copy this folder into the VS Code extensions folder and restart VS Code:

```bash
cp -R editors/vscode ~/.vscode/extensions/via-s-acc.signals-sig-0.1.0
```

or package it with `npx @vscode/vsce package` and install the `.vsix` with «Extensions: Install from VSIX…».

## Block data

`data/blocks.ru.json` and `data/blocks.en.json` are generated from the program; a test fails when they drift:

```bash
signals --lang ru blocks --json > editors/vscode/data/blocks.ru.json
signals --lang en blocks --json > editors/vscode/data/blocks.en.json
```
