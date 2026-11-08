+++
id = "export"
title = "Export to CSV and JSON"
theory = ["num-formats"]
source = "src/pipeline/blocks_dsp.cpp"
function = "StreamFile"
test = "tests/pipeline/test_blocks_dsp.cpp"
views = ["under_the_hood"]
+++

## Purpose

Write the result of a chain as text that numpy, pandas and spreadsheets read: `export` writes the samples of a signal to CSV or JSON, `export_report` the fields of a report (metrics, found frequencies, checks).

## Formulas

Numbers are written as `%.17g`: 17 significant digits guarantee that reading gives back the same double, bit for bit:

$$d = 1 + \big\lceil 53\log_{10}2 \big\rceil = 17$$

So $0.1$ is written as `0.10000000000000001`, and $1/3$ reads back exactly. One number takes at most 24 characters (sign, 17 digits, point, `e-308`), so the CSV of a signal with $C$ channels and $K$ rows weighs

$$\text{bytes} \le K\,\big(\lfloor\log_{10}K\rfloor + 2 + 25\,C\big)$$

Signal CSV: the header `n,ch0,ch1,…`, row $k$ is the index and the samples of every channel, $K = \max_c N_c$; a shorter channel leaves empty cells. Signal JSON: `{"rate":…,"channels":[[…],…]}`.

Report CSV: `field,value`; a string value is quoted only if it contains a comma, a quote or a line break, and inner quotes are doubled (RFC 4180); the field name is written as is. Report JSON: `{"passed":…,"fields":{…}}`; strings escape `"` and `\`, and bytes below 0x20 as `\u00XX`. NaN and infinity are written as `null` in JSON and in the report CSV, but as `nan` and `inf` in the signal CSV.

## Algorithm and cost

1. A relative path is taken from the chain's folder, an absolute one as is.
2. `export` accumulates text and flushes it in chunks of $2^{16}$ characters or more into a temporary file next to the target; cancellation is checked between chunks. On cancellation or an error the temporary file is removed and an older file at that path is left alone.
3. Completion is `fflush`, `fclose` and a rename of the temporary file onto the target: a reader sees either the old file or the complete new one.
4. `export_report` builds the whole text in memory and writes it the same way, "temporary file, then rename".
5. Both blocks are side effects: their result is never taken from the cache, and they run on every run of the chain. The report holds the path and the number of bytes written.

Time $O(K C)$; the memory of `export` is one chunk of $2^{16}$ characters, not the whole file.

## With this block's numbers

File {{block.path}}, format {{block.format}}. The node ran for {{block.wall}} and used {{block.bytes}}.

## Checks

- The CSV of two channels starts with `n,ch0,ch1`, the first row is `0,0.10000000000000001,1`; the JSON has `rate` 8000, and $1/3$ reads back exactly.
- `export_report` to JSON: the number 12.5 and a string with a quote read back, `passed` is true; in CSV a path with quotes and a comma is written as `"/tmp/my ""best"", take.wav"`.
- Writing into a missing folder fails and creates nothing; a path with Cyrillic letters works.
- Both blocks are marked as side effects; a cancelled export of 200,000 samples returns "cancelled" and leaves the folder empty.

## Links

- §13.1 — formats and precision: double, $u = 2^{-53}$.
- The "Write WAV" card — export to a sound file with quantization, whereas here the numbers lose not a single bit.

## Implementation

CSV rows accumulate in a buffer and go to disk in chunks, with a cancellation check.

```cpp
for (std::size_t k = 0; k < rows; ++k) {
    text += std::to_string(k);
    for (const auto& ch : a.channels) text += "," + (k < ch.size() ? fmt::format("{:.17g}", ch[k]) : std::string{});
    text += "\n";
    if (text.size() >= kChunk && !flush()) return core::make_error(core::Errc::cancelled, "export: cancelled");
}
```
