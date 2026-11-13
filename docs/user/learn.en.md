## Appendix. Learning path {#learn}

The learning path is a set of tasks the program checks by itself. Some tasks ask for a graph: a Haar bank, a convolution, a denoiser. Others ask for a number: a bank delay, an alias frequency, a count of factorizations. The tasks go from convolution to denoising and link to the sections of this book. The order is a recommendation; every task is open.

### Tasks in the interface {#learn-tui}

Tasks live on the "Theory ▸ Tasks" tab (`9`, then `]` until "Tasks").

- **On the left** — chapters and tasks. ✓ marks a solved task, ◐ one solved after reading the solution, ● the current task. The counter of solved tasks and the variant number are on top.
- **In the middle** — the statement with formulas and links to book sections.
- **Below** — the goals with ✓ and ✗ marks and the measured numbers, and next to them the hints and the solution.

Keys:

| Key | What it does |
|---|---|
| `Enter` | "Start": the task becomes current. A graph task opens its start graph in the Pipeline tab as a normal file; `Ctrl+S` saves the work and it stays between runs |
| `F2` | check the current task from any tab |
| `F3` | open the next hint from any tab |
| `a` | type the answer of an answer task; `Enter` checks it |
| `h`, `s` | a hint and the solution; before solving, the solution needs a confirmation |
| `t` | the book section of the task |
| `v` | the variant number |
| `Tab`, `z` | panels and a panel on the whole screen |

While a task is current, its line is shown above the hint row on every tab: the number, the title, "goals 2/4" or "passed ✓", and the `F2` and `F3` keys. A click on it opens the task. After every run of the work graph in the Pipeline tab the goals are rechecked by themselves. `Esc` cancels a long check.

Book sections that have tasks end with a "Tasks:" line linking to them.

### Taking the path from the command line {#learn-use}

The chapters and tasks with the solved ones marked:

```
signals learn
```

A task statement and the hints opened so far:

```
signals learn show 4.3
```

A graph task starts from a start graph. The command writes it to `4.3.sig` in the current folder and never overwrites an existing file:

```
signals learn start 4.3
```

Edit the graph in the Pipeline tab (`signals tui 4.3.sig`) or in any editor. To check it:

```
signals learn check 4.3 4.3.sig
```

An answer task has no file; the answer goes into the command. A vector is numbers separated by spaces or commas, and each number may be an expression: `sqrt(2)/2`, `1/3`, `2^-10`. In a single-number answer a comma is a decimal comma: `0,5`.

```
signals learn check 1.1 --answer "2 -3 0 7 1 -4"
```

Hints open one at a time:

```
signals learn hint 4.3
```

After solving, the worked solution is shown at once. Before solving, the program asks for `--yes` and marks the task as solved with the solution:

```
signals learn solution 4.3 --yes
```

Exit codes of `check`: 0 — passed, 3 — goals not met yet, 1 — the graph is missing or the task has an error, 2 — a wrong call.

### How a task is checked {#learn-check}

The check does not trust the student's nodes. It runs the graph itself, puts its own probes on the outputs it needs and computes measures: reconstruction error, SNR, delay, tone level, length. The task goals are tested on these measures. The goals are listed in the statement; after a check each one gets ✓ or ✗ and the measured value.

So that a task cannot be "solved" by fitting one answer, the check has three guards:

- **Hidden inputs.** The `samples` source node of the start graph gets other samples — random integers from −9 to 9 of the same length — and the graph runs several more times. A task passes only if the goals hold on every input. A ready answer typed into `samples` will not match on the hidden inputs.
- **Locked nodes.** Nodes the task asks to keep are compared with the start graph: their type and parameters must stay the same. The source values do not matter.
- **Conditions on the graph structure.** For example, "every path from `x` to `sum` goes through ↓2" or "the `dwt` block is not allowed". A wire `x → sum` that skips the bank fails such a condition.

One check is limited to 30 seconds; a graph that is too heavy gives an error instead of a hang.

### Progress, variants and the report {#learn-progress}

Progress lives in `learn.toml` in the settings folder ({#settings-dir}): the name, the variant number and, for every task, its state, the number of attempts and hints, the solve time and the last answer. A solved graph is copied to `learn/<number>.sig` next to it. If the progress file is damaged, the program keeps it as `learn.toml.bak` and starts over instead of failing.

Every student has a **variant** — a number from 1 to 999. The numbers in the tasks depend on it: the signal, the filter, the frequencies. The same task with the same variant gives the same numbers on any machine.

```
signals learn variant 17 --student "Anna Ivanova"
```

The report for the teacher is Markdown with the chapter list, the solved tasks, their goals with numbers, answers and graphs:

```
signals learn report -o report.md
```

The report states the variant, so the teacher can check an answer with the same command: `signals learn check 4.4 --variant 17 --answer 2`. All `signals learn` commands accept `--variant N` and `--progress FILE`: the first sets the variant for one command, the second uses another progress file.

### Your own course {#learn-course}

A course is a folder of chapters, and a chapter is a folder of tasks. Numbers come from the `NN-` prefixes: the folder `04-haar/03-build-bank` is task 4.3.

```
course.toml              title, title_en
04-haar/
  chapter.toml           title, title_en, theory = [book sections]
  03-build-bank/
    task.toml            the machine part
    start.sig            the start graph (graph tasks)
    solution.sig         the reference solution
    cheats/*.sig         shortcuts that must fail
    ru.md, en.md         statement, goals, hints, solution
```

`task.toml`:

```
kind = "graph"          # graph or answer
open = "pipeline"       # the tab "Start" opens
theory = ["haar-walkthrough"]
locked = ["x"]          # nodes that must not change

[[param]]               # variant numbers
name = "x"
kind = "ints"           # int, ints, real, choice
count = 8
min = -9
max = 9

[input]                 # hidden inputs
node = "x"
hidden = 4

[[measure]]
name = "err"
kind = "max_error"
a = "sum"
b = "x"
delay = "auto"

[[goal]]
when = "err <= 1e-12"

[[goal]]
through = { from = "x", to = "sum", block = "downsample" }
```

Use your course with `--course folder` on any `signals learn` command.

### Measures and goals {#learn-measures}

A measure is computed by the check from the graph outputs. The numeric fields `freq` and `delay` may be text with variant parameters: `freq = "{{f}}"`, `freq = "2 * {{f}}"`. `a` and `b` are a node or a band port (`dwt.d1`). Instead of `b` you may give `ref`: then the reference is the same node of the reference solution run on the same input.

| Kind | What it computes |
|---|---|
| `max_error` | the largest absolute difference of `a` and `b` after aligning the delay (`delay` is a number or `auto`), skipping `trim` samples at the edges |
| `snr` | the SNR of `a` against `b` in dB, with the same alignment |
| `delay` | the delay of `a` against `b` from the cross-correlation peak |
| `tone_db` | the level of the sine at `freq` in `a`, dB |
| `gain_db` | the difference of the tone levels at `freq` in `a` and `b`, dB |
| `max_abs`, `energy`, `rms_db`, `length` | the peak, energy, RMS level and length of `a` |
| `kept` | the share of nonzero coefficients in `a` |
| `field` | the field `field` of the report of node `a` |
| `samples` | all samples of `a` — for vector answers |
| `count` | the number of `block` nodes (bypassed nodes do not count) |
| `param` | the value of the parameter `node.param` |

A goal is exactly one of:

- `when` — an expression over measures and parameters: `err <= 1e-12 && n == 10`. It supports `+ - * / ^`, comparisons, `&& || !`, `abs min max sqrt log10` and `pi`;
- `through = { from, to, block }` — every path from `from` to `to` goes through a `block` block;
- `forbid = [...]` — blocks that are not allowed;
- `require = { block = number }` — blocks that must be there;
- `probe = "name"` — the graph has a probe with this name.

An answer task has no goals but an `[answer]` table: `type` (`number`, `integer`, `vector`, `choice`), `expected` — the name of a measure on the reference solution or an answer with placeholders (`"{{a}} + 1"`), `tol` and `rel` — the tolerance, `options` — the choices, `verify` — a condition on the reference measures that confirms the chosen answer.

### Task texts {#learn-text}

`ru.md` and `en.md` have the same shape:

```
# Title

The statement: paragraphs with formulas $…$ and book links {#section}.

## Goals {#goals}

1. The first goal's text — one item per [[goal]].

## Hints {#hints}

1. The first hint.
2. The second.

## Solution {#solution}

The solution with numbers.
```

Placeholders in the text and in `.sig` files: `{{x}}` is a variant parameter, `{{expected}}` the reference answer, `{{m.err}}` a measure on the reference solution. Take the numbers of a worked solution from placeholders: then the text never disagrees with the program.

### Checking a course {#learn-lint}

```
signals learn lint --course my-folder --variants 5
```

For every task and variants 1…N the command checks that:

- the reference solution passes and the start graph does not;
- every file in `cheats/` fails;
- `verify` is true;
- `ru.md` and `en.md` have the same number of hints;
- every book section and every placeholder resolves.

The built-in course passes the same check in the program's tests.
