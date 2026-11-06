# M3 — decisions made during execution

The plan `2026-11-06-signals-m3.md` was executed inline by one implementer, then reviewed once as a whole branch. Every place the implementation departs from the plan, or settles something the plan left open, is recorded below with its reason and what it costs if wrong.

## Rulings

- Task 1: Ruling: CLI tui tests live in tests/cli/test_cli_tui.cpp (the brief named only harness files) and add a 'tui without a file' case; the no-argument TTY branch of main is checked by hand (signals < /dev/null prints help) since isatty cannot be faked in-process — cost if wrong: none.
- Task 3: Ruling: theme files use token keys trace1…trace8 as named in §8.2; a theme missing tokens lists all of them in one message; the light theme keeps the fixed trace colours and uses black for the reference trace (visualization.md §4.2: reference white or black) — cost if wrong: one theme file.
- Task 4: Ruling: locale precedence LC_ALL > LC_CTYPE > LANG (POSIX), TERM=dumb forces ascii even with a UTF-8 locale; process_env() added as the real EnvLookup; to_string for ColorDepth/GlyphMode added for the prefs file — cost if wrong: none.
- Task 6: Ruling: PlotSpec, Trace and Envelope fields get default member initialisers so partial aggregate initialisation is free of -Wmissing-field-initializers (CI builds own targets with -Werror) — cost if wrong: none.
- Task 6: Ruling: the "focused trace owns shared cells" test uses two overlapping traces with the focused one listed first — crossing lines in a 10x4 field pass through neighbouring cells and share none — cost if wrong: none.
- Task 6: Ruling: cursors are drawn as a full-height │ column (cell resolution, as visualization.md §4.4 allows without SGR-pixels), axis_fraction() is exported for screens that map mouse to values — cost if wrong: none.
- Task 7: Ruling: shaded fallback (ansi16/none or ascii glyphs) uses one glyph per cell from the mean of its two pixels in the theme fg colour; z-plane multiplicity in ascii mode is plain digits (x2); utf8/split_codepoints/superscript moved to widgets/text_util.hpp for reuse — cost if wrong: none.
- Task 8: Ruling: the cache test compares a cached wavelet("sym17") against the uncached symlet(17) instead of two wavelet() calls — other tests compute all 106 wavelets first, so a "first call" is not cold in a full run — cost if wrong: none.
- Task 8: Ruling: welch_psd also rejects fs <= 0; Errc::cancelled gets error.cancelled in ru.json and the i18n key test's code list — cost if wrong: none.
- Task 9: Ruling: submit() does not notify (it runs on the UI thread, which redraws after the event anyway); throttling is tested as "notify count <= elapsed / interval + 2" instead of a fixed 4 so CI timing jitter cannot flake it; extra tests: queued-job cancel, fail(), skipped unstarted stages, history of 20 — cost if wrong: none.
- Task 10: Ruling: CPU time on both platforms comes from getrusage(RUSAGE_SELF) (counts finished threads too), shared code lives in sampler_common.cpp; ResourceMonitor gains samples() for the history test; Linux sampler is only syntax-checked locally (g++-16 -fsyntax-only) and gets its real run in CI — cost if wrong: Linux monitor fix after CI.
- Task 11: Ruling: UiContext gains terminal_size (the shell must know the breakpoint before building elements; tests inject it) and holds a non-const ResourceMonitor* (frame_rendered counts FPS); TabScreen gains capturing_text() so a screen in text-input mode gets digits and q first — cost if wrong: an interface tweak.
- Task 11: Ruling: at the small breakpoint the tab row is compact (active tab with its name, others as digits) — full labels do not fit 80 columns; panels get one column of inner padding as on the canvas; placeholder_screens takes the UiContext so ASCII/colour settings reach them; Ctrl+C is left to FTXUI's default handler (exits) — cost if wrong: visual only.
- Task 12: Ruling: UiContext gains history, prefs_file and first_run_check (tests inject them; run() fills them from the monitor and prefs_path(env)); the terminal check is driven by keys 1–4 (colour), u/a (glyphs), s (save) — clickable options deferred; plot y axis gets at least 3 ticks (with 2, Heckbert picks a step wider than the range); overlay snapshots added for palette, check and resources — cost if wrong: none.
- Task 13: Ruling: SignalScreen is a public class with area(id) next to make_signal_screen — the mouse test needs plot and panel boxes — cost if wrong: one header to hide later.
- Task 13: Ruling: SignalData adds band_lengths and display_memory (pyramids tracked as display) — bands panel shows lengths, resources panel must see display buffers — cost if wrong: two fields.
- Task 13: Ruling: errc_name moved from commands into core/error.hpp; TUI error_text formats like the CLI — cost if wrong: one function to move back.
- Task 13: Ruling: plot gains Envelope::provider, PlotSpec::x_labels and plot_area() — pyramid queried at the real dot width; L/R share one x axis; mouse maps to the plot area — cost if wrong: widget API churn.
- Task 13: Ruling: view state resets on the UI thread at the first read after a new SignalData is published (not in the worker) — keeps view state single-threaded — cost if wrong: one-frame lag after load.
- Task 13: Ruling: `o` opens the path prompt through a new UiContext::ask_open set by the shell; empty state reads «o — открыть WAV, : — команды» — cost if wrong: one callback.
- Task 13: Ruling: shell hint row keeps global hints and drops screen hints that do not fit — at 80 columns «q выход» was pushed off — cost if wrong: fewer screen hints visible.
- Task 13: Ruling: open() cancels the previous load job; the «открыт» log line carries no load time (run panel shows elapsed) — the time overflowed 37 columns and made the snapshot unstable — cost if wrong: load time only in the run panel.
- Task 13: Ruling: snapshot test normalises wall-clock and measured durations (hh:mm:ss, " <t>" swallowing padding) — stage times are measured — cost if wrong: snapshot misses a time-format regression.
- Task 13: Ruling: tests "keys switch panels…" and "ascii mode…" were written after the code and passed first run — they pin brief behaviour (Tab, z, Esc, arrows, Home/End, poor terminal) — cost if wrong: weaker evidence for those keys.
- Task 14: Ruling: each wavelet is one library leaf with an h0 ref; the screen switches the role — the brief requires every name exactly once — cost if wrong: library shows roles one level deeper.
- Task 14: Ruling: ComparisonSet::Entry adds `error`, and the set exposes reference() and focus() — the screen must show a failed analysis and the reference/focus marks — cost if wrong: three members.
- Task 14: Ruling: orth_error only for orthogonal families (nullopt for bior/rbio); zeros at z = −1 only for h0/f0; Sobolev of f0 for synthesis roles, of h0 otherwise — a biorthogonal bank is not meant to be orthogonal, and h1/f1 vanish at z = +1 — cost if wrong: fewer numbers on screen.
- Task 14: Ruling: subscript digits moved to text_util.hpp (subscript_count) and shared with the signal screen — cost if wrong: none.
- Task 15: Ruling: FiltersScreen is a public class with area(), view(), cursor_a()/set_cursor_a(), omega_at() for tests, next to make_filters_screen — cost if wrong: one header to hide.
- Task 15: Ruling: ComparisonSet gains pending tracking (no duplicate jobs), request_view()/Entry::view for the zoomed recompute, and free range_response()/masked_group_delay(peak) — the brief's zoom recompute needs a sub-band response the core lacks — cost if wrong: model API churn.
- Task 15: Ruling: the group-delay mask is tested at model level plus a no-"nan" render — text frames cannot tell trace dots from grid dots in the first column — cost if wrong: a render-only regression goes unseen.
- Task 15: Ruling: the exact "−3,01" readout test sets cursor A directly; the click test checks the cursor lands within one column of π/2 — a click lands on a column centre, which moves |H| by up to 0.1 dB — cost if wrong: none.
- Task 15: Ruling: library shows family chips and the names of the chosen family; leaves add the role chosen by `t` or the role chips (h₀ default) — the brief fixes one leaf per name — cost if wrong: one more click for h₁/f₀/f₁.
- Task 15: Ruling: T(z) and A(z) are never normalised; phase is masked like group delay where |H| < 10⁻³·max — A(z) is round-off noise and would be drawn at 0 dB; phase is meaningless at zeros — cost if wrong: different curves for chain filters.
- Task 15: Ruling: «у эталона колонки Δ» = Δ(−3 дБ) and Δ(подавл. ПЗ) columns against the reference on every other row; the reference row reads «эталон» — cost if wrong: other Δ columns wanted.
- Task 15: Ruling: readouts show magnitude for all visible traces at A, at B and B−A; phase/delay values are not read out; context shows normalised ω without fs — the screen does not know the open signal — cost if wrong: missing kHz in readouts.
- Task 15: Ruling: left column is 33 wide at primary (canvas 31), the comparison panel has no hint line (keys live in the hint row), the app preloads h₀/h₁ db2 into the comparison — labels truncated at 31; an empty tab on first launch says nothing — cost if wrong: 2 columns of plot width.
- Task 15: Ruling: format_fixed (format.hpp) and display_width/pad_left/pad_right (text_util.hpp) are shared by both screens — cost if wrong: none.
- Task 16: Ruling: status-line memory on macOS is the physical footprint (proc_pid_rusage, what top and Activity Monitor show), Linux stays on RSS; the monitor test now compares against that counter — resident_size kept freed-reusable pages (711 MB after a 10-minute load against 252 MB live), and the M3 gate says «совпадают с top» — cost if wrong: macOS readings below RSS.
- Task 16: Ruling: the loader frees each double channel right after its float copy and mono share — peak while opening 10-minute stereo dropped from 1.04 GB to 660 MB; evidence is a measurement in the release app, not a unit test — cost if wrong: none functionally.
- Task 16: Ruling: elapsed time in the run panel has a fixed width of 8; PR errors below 1e-15 read «< 10⁻¹⁵» — snapshots differed between clang and GCC (round-off 1.4e-17 vs 5.6e-17, time width) and the elapsed line jittered on screen — cost if wrong: less precision shown for round-off.
- Task 16: Ruling: TUI benchmarks live in bench/bench_tui.cpp inside signals_bench (one binary, as the brief's run command); the cached wavelet() rows are relabelled — cost if wrong: none.
- Task 16: Ruling: acceptance against top ran through pty scripts (idle, wheel storm, live resizes 150×44→80×24→60×20) instead of a manual side window — the user was away; numbers are in §11 — cost if wrong: no human look at the screens before review.
- Final: Ruling: unlocked `stop_` read in JobRunner::loop removed without a failing test — the race is real by the memory model, but the mutex in retire() orders it in every observed run, so TSan (3 runs of the threaded tests) cannot show it — cost if wrong: none, the wait predicate already covers stop.
- Final: Ruling: the sym17 pending-analysis test now waits up to 30 s (was 4 s) — under TSan the first sym17 design exceeds 4 s; the loop exits as soon as the analysis lands — cost if wrong: a hung analysis takes 30 s to fail the test.
- Final: Ruling: exceptions inside the load job and the filter-analysis job are caught, logged in Russian («не хватило памяти» / «внутренняя ошибка — …») and clear the pending flags (view flag via a scope guard); no failing test — nothing in the code paths can be made to throw without an injection hook — cost if wrong: an untested catch.
- Final: Ruling: rulings 24, 30, 46, 60, 64 stand — Linux sampler runs in CI on the PR; check-overlay mouse support and h₁ zeros at z = +1 are polish; the §8.8/§11 edits and the missing human visual pass go to the user in the final report — cost if wrong: user disagrees with the spec edit or finds a visual defect.
- Final: Ruling: reviewer's declined-to-judge list accepted as is (tiny files without spectrum, NO_COLOR selection tint, Cyrillic in ASCII mode, terminal mouse limits, inline-mode test hazards) — each has a visible fallback or is test-only — cost if wrong: small UX gaps.

## Final review

- Final: review — 0 critical, 6 important, 13 minor, 6 disputed rulings; reviewer used 351k tokens.
- Final: fixed interrupted stage shown as done — test "a cancelled job skips its remaining stages" (stage 0 now skipped) RED→GREEN.
- Final: fixed raw «({details})» after Esc — test "a cancelled load reads as interrupted without raw placeholders" RED→GREEN.
- Final: fixed 256-colour frames over budget (21.7/19.1 ms) — test "256-colour lookups are cheap enough for a frame" RED (105 ms for 5000 lookups) → GREEN; bench now 0.22/0.60 ms.
- Final: fixed off-view cursors and selections pinned to the plot edge — test "cursors and selections outside the axis are not pinned to the edge" RED→GREEN.
- Final: fixed unbounded envelope fill (and lround overflow on huge samples) — test "envelopes far outside the axis are clipped*" RED (3.2 s per frame) → GREEN.
- Final: fixed theme background never painted — test "the theme background is painted unless colour is off" RED→GREEN.
- Final: fixed no guard for huge WAV — io::probe_wav reads the header, the load is refused above half of physical RAM (estimated frames × (13·channels + 16) bytes) with a journal line — tests "probing a wav reads only its header" and "a file too large for memory is refused with a journal entry" RED (compile) → GREEN.
- Final: fixed empty selection reading as 0 dBFS — test "an empty selection has no level, not full scale" RED→GREEN.
- Final: fixed NO_COLOR overridden by saved prefs and empty NO_COLOR disabling colour — test "NO_COLOR wins over saved preferences and must be non-empty" RED→GREEN.
- Final: fixed first-run check reappearing after Esc — test "closing the first-run check with Esc stops it reappearing" RED→GREEN; a corrupt prefs file now shows the check instead of hiding it (run.cpp, no unit test: run() needs a terminal).
- Final: fixed path prompt taking `~`, quotes and backslash escapes literally — test "typed and dropped paths are expanded" RED (compile) → GREEN.

## Deferred minors

- run panel forgets the load job after ~20 newer jobs (history of 20).
- macOS memory test compares with the same proc_pid_rusage counter the sampler reads; the top comparison is manual evidence.
- layout heights on «Фильтры» and «Сигнал» change with filter and band counts, not only on resize.
- unit strings (с, Гц, к, дБFS, Б) are inline in signal_screen, status and format instead of the dictionary.
- `signals tui` does not check that stdin/stdout are terminals.
- terminal-check options are keyboard-only (ruling 30).
- h₁/f₁ show «—» for ВМ and band metrics; their zeros at z = +1 and mirrored bands are not reported (ruling 46).
