# One cutoff for two filters

The input `x` is the sum of a tone at $f_1 = 500$ Hz and a tone at $f_2 = 3000$ Hz with amplitude {{a2}} at $f_s = 8000$ Hz. It is split into two bands: the lowpass `lo` keeps the lower tone, the highpass `hi` the upper one. Right now the filters have different cutoffs, both poor. Set the cutoff with one variable (`let`) and use it in both filters, so the bands always meet at one point: change the cutoff in one place and both filters follow. Do not change the sources `g1`, `g2` and the adder `x`.

## Goals {#goals}

1. `lo` passes 500 Hz losing less than 1 dB and suppresses 3000 Hz by at least 40 dB.
2. `hi` passes 3000 Hz losing less than 1 dB and suppresses 500 Hz by at least 40 dB.
3. Some variable is used at least twice.

## Hints {#hints}

1. The cutoff must lie between the tones so that the transition band of a 101-tap filter (about 250 Hz with a Hamming window) touches neither of them ({#freq-band-metrics}).
2. A variable is declared with the line `let fc = 1500`, and a param says `cutoff=$fc`. In «Pipeline» variables are edited in the «Variables» panel (`Tab` to it, `+` adds one); the rules are in {#sig-vars}.
3. Two equal numbers in two nodes give the same levels but fail the goal about the variable: they drift apart at the first edit of one of them.

## Solution {#solution}

`let fc = 1500` with `cutoff=$fc` in both filters works. The levels of the reference: the lower tone in `lo` is {{m.lo_pass}} dB, the upper one in `lo` {{m.lo_stop}} dB; the upper tone in `hi` is {{m.hi_pass}} dB, the lower one in `hi` {{m.hi_stop}} dB. A 1500 Hz cutoff is far from both tones, and the Hamming window gives about 53 dB of suppression ({#cls-window-method}). The variable makes the cutoff one number of the graph: `--set fc=2000` on the command line or an `S` series over the variable moves both filters at once, and the Python script gets the line `fc = 1500` at its top.
