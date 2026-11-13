# The Haar bank with a spoiled f₁

In the Haar bank of task 4.3 the synthesis filter $f_1$ is spoiled: a block `invert` follows it, so the sign of the detail branch is changed. The input is `x = [{{x}}]`, the bank output is node `sum`. What is the error $\max|\hat x - x|$ over all samples (the delay is zero)? The answer is a number; tolerance $10^{-9}$.

## Hints {#hints}

1. A sound bank returns $\hat x = x_{\text{low}} + x_{\text{high}}$, where $x_{\text{low}}$ is the contribution of branch $a$ and $x_{\text{high}}$ that of branch $d$. The spoiled one gives $x_{\text{low}} - x_{\text{high}}$.
2. So $\hat x - x = -2\,x_{\text{high}}$. For one pair $(x[2n], x[2n+1])$ the detail contribution is $\pm\tfrac12\,(x[2n+1] - x[2n])$ on the two neighbouring samples.
3. The error on a pair is $|x[2n+1] - x[2n]|$ on both of its samples; take the largest over all pairs. Run the graph and compare with a `check` block with `kind=error`.

## Solution {#solution}

The error: {{expected}}.

The sum $x_{\text{low}} + x_{\text{high}}$ restores the signal because the aliasing of the two branches cancels ({#fb-distortion-alias}). Flipping the sign of $f_1$ breaks the cancellation: $x_{\text{high}}$ enters with a minus, and the error is twice the detail contribution, that is $\max_n |x[2n+1] - x[2n]|$. Had the signal been constant on every pair, the details would be zero and this fault would go unnoticed.
