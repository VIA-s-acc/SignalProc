# Energy after the analysis matrix

The analysis matrix $A$ of the `{{wv}}` bank for $N = 16$ with periodization is square, $16 \times 16$: the vector $Ax$ collects all coefficients of bands $a$ and $d$ of one level. For the signal `x = [{{x}}]` find the squared norm $\|Ax\|^2 = \sum_k (Ax)_k^2$, the total energy of all coefficients. The answer is a number.

## Hints {#hints}

1. The `{{wv}}` bank is orthogonal. Open the "Matrix" tab and compare $A^{\mathsf T}A$ with the identity matrix; see {#fbm-periodization}.
2. For an orthogonal matrix $\|Ax\|^2 = x^{\mathsf T}A^{\mathsf T}Ax$. What is it when $A^{\mathsf T}A = I$?
3. The resulting number can be computed from $x$ by hand or with a calculator; the coefficients themselves are not needed. Cross-check with a run in the pipeline: `dwt` with periodization, one level.

## Solution {#solution}

$\|Ax\|^2 = {{expected}}$. The bank is orthogonal, so $A^{\mathsf T}A = I$ and $\|Ax\|^2 = x^{\mathsf T}x = \sum_n x[n]^2$: the energy of the samples is preserved for any number of vanishing moments and any filter length; this is Parseval's equality for the periodized transform, whose matrix is square. For a biorthogonal bank it fails: there $A^{\mathsf T}A \ne I$. See {#fbm-periodization} and {#met-parseval}.
