## Appendix. Settings and files {#settings}

Signals keeps little on disk: terminal and editor settings in `terminal.toml`, the session (theme, last tab, recent audio files and graphs) in `session.toml`, and a folder with your own filters. It has no cache or logs; everything else it writes goes where you tell it to. This appendix lists where these files are, which keys they hold and how to change them.

### The settings folder {#settings-dir}

The folder is chosen at start by the first rule that applies:

| Condition | Folder |
|---|---|
| `XDG_CONFIG_HOME` is set (on any system) | `$XDG_CONFIG_HOME/signals/` |
| Windows and `APPDATA` is set | `%APPDATA%\signals\` |
| otherwise | `~/.config/signals/` (`$HOME/.config/signals/`) |

On macOS this is also `~/.config/signals/`, not `~/Library/Application Support`. The program creates the folder and the files the first time it saves something. If even `HOME` is unset, settings go to `./.config/signals/` in the current folder and filters to `./signals-filters/`.

The folder holds five things:

| Path | What it is |
|---|---|
| `terminal.toml` | colour, glyphs, language, editor and the terminal-check mark |
| `session.toml` | the session: theme, last tab, recent audio files and pipelines |
| `filters/*.flt` | "My filters": your own banks and single filters |
| `learn.toml` | learning path progress: the name, the variant, the solved tasks ({#learn-progress}) |
| `learn/` | the solved task graphs and the work graphs in `learn/work/` |

### terminal.toml {#settings-terminal}

A plain TOML file. Example:

```
checked = true
color = "truecolor"
glyphs = "unicode"
language = "en"
editor = "code -w"
```

| Key | Values | When missing | Written by |
|---|---|---|---|
| `checked` | `true`, `false` | `false`: the terminal check opens at start | the terminal check (`Esc` on the first run, or `s`) |
| `color` | `none`, `ansi16`, `palette256`, `truecolor` | detected from the environment | the terminal check, key `s` |
| `glyphs` | `unicode`, `ascii` | detected from the locale | the terminal check, key `s` |
| `language` | `ru`, `en` (`en_US`, `english`, `русский` are understood too) | Russian | the palette commands "Language: …" |
| `editor` | an editor command, e.g. `code -w` | `VISUAL`, then `EDITOR`, then the system one | the palette command "Editor for sources…" |

The program rewrites the whole file from these five keys, so any other key disappears at the next save. If the file does not parse as TOML, the program runs with the defaults and shows the terminal check again.

The terminal check can be opened at any time: palette `:`, command "Terminal check". Digits `1`–`4` try colour depths, `u` and `a` try glyphs, `s` saves the choice to `terminal.toml`.

### Editor for sources {#settings-editor}

On "Theory ▸ Under the hood", `o` shows a block's source inside the program, and `e` or the "open in the editor" button open the same file in an external editor. This needs the repository with the sources next to the program. The editor is chosen in this order:

1. the `editor` key in `terminal.toml`, if it is not empty;
2. the `VISUAL` variable;
3. the `EDITOR` variable;
4. the system program: `open -t` on macOS, `xdg-open` on Linux, `notepad` on Windows.

The easiest way to set it is from the program: palette `:`, command "Editor for sources…", type the command and press `Enter`. An erased field and `Enter` remove the setting, and `VISUAL`, `EDITOR` and the system program apply again. The command is split on spaces and the file path is added as the last word; quotes inside the command are not supported.

| Editor | Command |
|---|---|
| VS Code | `code -w` |
| Cursor | `cursor -w` |
| Sublime Text | `subl -w` |
| Zed | `zed -w` |
| Neovim, Vim | `nvim`, `vim` |
| nano | `nano` |

A console editor opens in the same terminal: the Signals interface steps aside and comes back when the editor closes. If VS Code has no `code` command in `PATH`, install it from VS Code (palette ⇧⌘P, "Shell Command: Install 'code' command in PATH") or give a full path without spaces.

### My filters: the filters folder {#settings-filters}

Each of your filters or banks is a separate `.flt` file in `filters/`. Files appear when you save a bank in the "Designer" or enter `name = coefficients` with `c` on the "Filters" tab. At start the program reads every `.flt` in the folder; a file with an error is skipped and the reason shows in the log of the "Signal" tab.

A bank:

```
name = "my_bank"
kind = "bank"
h0_offset = 0
h0 = [0.48296291314469025, 0.836516303737469, 0.22414386804185735, -0.12940952255092145]
f0_offset = 0
f0 = [-0.12940952255092145, 0.22414386804185735, 0.836516303737469, 0.48296291314469025]
note = "Daubechies 2, double-checked"
```

A single filter:

```
name = "h1"
kind = "filter"
h_offset = 0
h = [0.5, -0.5]
note = ""
```

| Key | Bank | Filter | Meaning |
|---|---|---|---|
| `name` | yes | yes | the name in lists; the file name if missing |
| `kind` | `"bank"` (default) | `"filter"` | what the file holds |
| `h0`, `f0` | required | — | analysis and synthesis lowpass; the highpass filters follow from them |
| `h` | — | required | the filter coefficients |
| `h0_offset`, `f0_offset`, `h_offset` | optional | optional | index of the first tap, an integer, 0 by default |
| `note` | optional | optional | a note shown next to the name |

The file name comes from the filter name: spaces become `_`, punctuation is dropped, letters of any alphabet stay. If the name is taken, `-2`, `-3` and so on are added. Files can be written and edited by hand: the program reads them after a restart, and refuses a bank without perfect reconstruction, saying why. The VS Code extension in `editors/vscode` highlights `.flt` files and checks a bank as you type.

Your banks work wherever a wavelet or a filter is chosen: on the tabs, in pipeline blocks (`dwt(wavelet=my_bank)`, `convolve(filter=my_bank.h0)`) and on the command line (`signals filter my_bank`).

### Environment variables {#settings-env}

| Variable | Effect |
|---|---|
| `SIGNALS_LANG` | interface and message language (`ru` or `en`); stronger than the saved language, weaker than `--lang` |
| `XDG_CONFIG_HOME` | root of the settings folder on any system |
| `APPDATA` | root of the settings folder on Windows when `XDG_CONFIG_HOME` is unset |
| `HOME` | `~/.config/signals/` and `~` in paths typed in the program |
| `VISUAL`, `EDITOR` | editor for sources when the `editor` key is not set |
| `NO_COLOR` | any non-empty value turns colour off, even one saved in `terminal.toml` |
| `TERM` | `dumb` turns off colour and Unicode; `…256color` gives 256 colours |
| `COLORTERM` | `truecolor` or `24bit` give 24-bit colour |
| `TERM_PROGRAM` | `Apple_Terminal` gives 256 colours |
| `WT_SESSION` | Windows Terminal: 24-bit colour and Unicode |
| `LC_ALL`, `LC_CTYPE`, `LANG` | the first non-empty one decides Unicode or ASCII: UTF-8 means Unicode |

The language is chosen in this order: `--lang`, then `SIGNALS_LANG`, then `language` in `terminal.toml`, then Russian. Commands outside the interface (`signals run`, `signals filter` and the others) do not read the settings file: only `--lang` and `SIGNALS_LANG` apply to them.

### What is saved and what is not {#settings-session}

| Action | Saved |
|---|---|
| "Language: Russian" and "Language: English" in the palette | yes, key `language`; the interface restarts |
| `s` in the terminal check | yes, keys `color`, `glyphs`, `checked` |
| "Editor for sources…" in the palette | yes, key `editor` |
| saving a bank in the "Designer", `c` on "Filters" | yes, a file in `filters/` |
| "Theme: light" and "Theme: dark" in the palette | yes, key `theme` in `session.toml` |
| an opened audio file, an opened or saved pipeline | yes, the `audio` and `graphs` lists in `session.toml` |
| the tab the program was closed on | yes, key `tab` in `session.toml` |
| "Glyphs: …", "Color: …" in the palette | no, only until exit |
| `--lang`, `--theme` on the command line | no, only for this run |

### session.toml {#settings-session-file}

The program writes the session itself; there is no need to edit it by hand. An example:

```toml
theme = "light"
tab = 7
audio = ["/home/u/rec/take.flac", "/home/u/rec/test.wav"]
graphs = ["/home/u/sig/denoise.sig"]
```

| Key | Meaning |
|---|---|
| `theme` | `dark` or `light`, the theme chosen in the palette; `--theme` on the command line wins and is not saved |
| `tab` | the zero-based number of the tab the program was closed on |
| `audio` | up to 10 recent audio files, newest first; a file gets here once it has been read without an error |
| `graphs` | up to 10 recent `.sig` pipelines opened or saved in the "Pipeline" tab |

`signals tui` without a file opens the first existing file from `audio`, the first existing pipeline from `graphs` and the tab `tab`. With a file on the command line, that file and the "Signal" tab open, and the pipeline and tab are not restored. The palette command "Open recent…" shows both lists (missing files are hidden), "Forget recent files" clears them. A broken `session.toml` does not stop the start: the program begins with an empty session and rewrites the file.

### Files next to pipelines {#settings-outputs}

The `export`, `export_report` and `wav_out` blocks write where their `path` parameter points. A relative path is resolved like this:

| How the pipeline runs | Relative to |
|---|---|
| `signals run pipeline.sig` | the folder of the `.sig` file |
| "Pipeline" tab, pipeline not saved yet | the folder `signals tui` was started from |
| "Pipeline" tab, pipeline opened or saved | the folder of the `.sig` file |

When a pipeline is saved to another folder, the program rewrites relative paths so that they still lead to the same places. Writing goes through a temporary file `.name.XXXXXXXXXXXXXXXX.partial` next to the target, which is then renamed; if a run is interrupted, such a file may stay behind and can be deleted. `--probe-dir` and `-o` of `signals run`, and `--bands-dir` of `signals dwt`, are relative to the current folder.

### Resetting everything {#settings-reset}

Delete `terminal.toml`: the next start shows the terminal check, the language becomes Russian and the editor falls back to `VISUAL`, `EDITOR` and the system program. Delete `session.toml` to forget the theme, the tab and the recent files. Delete the `filters/` folder or single `.flt` files to remove your filters. The program keeps nothing else.
