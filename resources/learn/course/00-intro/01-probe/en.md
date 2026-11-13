# Place a probe

The start graph is complete and runs: a sine $x$ of {{f}} Hz goes through the moving average `y`. But there is nothing to look at the output with: the graph has no probes. A probe shows the signal at a node output in the "Probe" panel; without it the graph is computed and you see no result.

Put a probe named `y` on node `y`. Leave the rest of the graph as it is.

## Goals {#goals}

1. The graph has a probe named `y`.

## Hints {#hints}

1. Select node `y` in the "Pipeline" tab and look at the keys at the bottom of the screen: a node has a "place probe" command.
2. In a `.sig` file a probe is the line `probe node as "name"`. The name is quoted and must be unique.
3. After a run (`r`) the "Probe" panel must show a curve with that name. The name is all the check needs: it looks for a probe called `y`.

## Solution {#solution}

One line is needed: `probe y as "y"`. The node name and the probe name need not match, but the graph reads easier that way. Probes do not change the signal: they only show what flows along the wire. The conventions for signals and notation are collected in {#conventions}.
