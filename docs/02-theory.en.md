# Theory: filter banks, wavelets, Toeplitz matrices {#theory}

A theory reference for rewriting an old Python toolkit in C++ (a two-channel perfect-reconstruction filter bank from the factorization of the halfband $P_0$, the Mallat pyramid, zeroing WAV subbands). The document fixes the conventions, formulas, algorithms, their cost, the numerical traps and **the checks the implementation must pass**. Each subsection is self-contained: purpose → formulas → algorithm and complexity → how to check.

Implementation priority tags:

- **[core]** — needed in the first working version of the library and tests;
- **[v1]** — the first full release;
- **[v2]** — the next stage;
- **[research]** — optional, experiments.

Numbers marked "verified" were obtained numerically (Python 3.13, NumPy 2.3, PyWavelets 1.8) while preparing this document; the corresponding checks must be carried over to the C++ tests.

---

## 0. Conventions and notation [core] {#conventions}

### 0.1. Signals and transforms {#conv-signals}

- A signal $x[n]$, $n\in\mathbb Z$; a finite signal of length $N$ has indices $0..N-1$.
- $z$-transform: $X(z)=\sum_n x[n]z^{-n}$. A delay by $k$ samples is multiplication by $z^{-k}$.
- DTFT: $X(e^{i\omega})=\sum_n x[n]e^{-i\omega n}$, $\omega\in[-\pi,\pi)$ rad/sample; the frequency in hertz is $f=\omega f_s/(2\pi)$.
- DFT of length $N$: $X[k]=\sum_{n=0}^{N-1}x[n]e^{-2\pi i kn/N}$; matrix $F_{kn}=e^{-2\pi i kn/N}$, $F^{-1}=\tfrac1N F^{*}$.
- Time reversal $h^-[n]=h[-n]\leftrightarrow H(z^{-1})$; modulation $(-1)^n h[n]\leftrightarrow H(-z)$ (shifts the magnitude response by $\pi$).

### 0.2. A filter as a Laurent polynomial {#conv-laurent}

A filter is stored as a pair (start index $n_0$, coefficient array): $H(z)=\sum_{n=n_0}^{n_1}h[n]z^{-n}$, length $L=n_1-n_0+1$. Negative $n_0$ is allowed (non-centered and symmetric filters, lifting). All operations (convolution, modulation, polyphase decomposition, reversal) are defined on this type.

**Trap.** The modulation $(-1)^n h[n]$ depends on the *absolute* index: shifting a filter by an odd number of samples flips the sign of the highpass. An array without an offset loses this information — that is why the offset is always stored.

### 0.3. Normalization {#conv-normalisation}

- **Primary (orthonormal):** $H_0(1)=F_0(1)=\sqrt2$; for orthogonal filters $\sum h_0[n]^2=1$. This is the PyWavelets normalization.
- **Sum (Strang–Nguyen, old code):** $H_0(1)=1$, $F_0(1)=2$. Keeps the coefficients rational (Haar, 5/3, spline CDF) and suits exact arithmetic.
- In both, $P_0(1)=H_0(1)F_0(1)=2$. Conversion: $h_0\cdot\sqrt2$, $f_0/\sqrt2$.

### 0.4. Highpass signs and the PyWavelets correspondence {#conv-pywt}

Bank: analysis $h_0$ (lowpass), $h_1$ (highpass); synthesis $f_0$, $f_1$. Analysis channel $v_i[k]=(h_i*x)[2k]$, synthesis $\hat x=\sum_i f_i*(\uparrow2\, v_i)$.

| role | Strang–Nguyen / old code | PyWavelets |
|---|---|---|
| analysis lowpass | $h_0$ | `dec_lo` $=h_0$ |
| synthesis lowpass | $f_0$ | `rec_lo` $=f_0$ |
| analysis highpass | $H_1(z)=F_0(-z)$, $h_1[n]=(-1)^n f_0[n]$ | `dec_hi`$[n]=(-1)^{n+1}$`rec_lo`$[n]=-h_1[n]$ |
| synthesis highpass | $F_1(z)=-H_0(-z)$, $f_1[n]=(-1)^{n+1}h_0[n]$ | `rec_hi`$[n]=(-1)^n$`dec_lo`$[n]=-f_1[n]$ |

Here $n$ is the array index starting from 0. The PyWavelets highpass pair equals the Strang–Nguyen pair with a minus sign; the product $F_1H_1$ is the same, all PR conditions coincide, and the detail coefficients change sign. Verified on PyWavelets 1.8:

1. All four arrays have the same length; in `bior*` the short filters are padded with zeros — strip the zeros on load, keeping the offset.
2. `dwt` computes the full convolution of the extended signal and takes the **odd** samples: `cA[k]` $=(h_0*\tilde x)[2k+1]$. In our "even" notation these are the filters $zH_i(z)$; for Haar this gives reconstruction without delay (§5).
3. Orthogonal families: `dec_lo` = reversed `rec_lo`; `rec_lo` holds the classical Daubechies coefficients (db2: $0.48296, 0.83652, 0.22414, -0.12941$), minimum-phase.
4. `biorNr.Nd`: `rec_lo` has $N_r$ zeros at $z=-1$, `dec_lo` has $N_d$. Hence the analysis details annihilate polynomials of degree $<N_r$ (verified: `bior3.1` annihilates $t^0..t^2$, `bior1.3` only constants), and the synthesis wavelet has $N_d$ vanishing moments. `rbioNr.Nd` — the roles are swapped. `bior4.4` and `bior6.8` are **not spline wavelets**: they are CDF 9/7 and 17/11 (the lengths of the nonzero parts are verified).

Compatibility mode: the C++ library reproduces `pywt.dwt/idwt` for every boundary mode to within $10^{-12}$ (double); the sign of the details is set by an option.

### 0.5. Polyphase form {#conv-polyphase}

Signal phases: $x^{(0)}[n]=x[2n]$, $x^{(1)}[n]=x[2n+1]$, $X(z)=X^{(0)}(z^2)+z^{-1}X^{(1)}(z^2)$.

- Analysis: $H(z)=H^{(0)}(z^2)+zH^{(1)}(z^2)$, $H^{(0)}(z)=\sum_k h[2k]z^{-k}$, $H^{(1)}(z)=\sum_k h[2k-1]z^{-k}$.
- Synthesis: $F(z)=F^{(0)}(z^2)+z^{-1}F^{(1)}(z^2)$, $F^{(0)}(z)=\sum_k f[2k]z^{-k}$, $F^{(1)}(z)=\sum_k f[2k+1]z^{-k}$.

Then $V=H_p(z)X_p(z)$ and $\hat X_p=F_p(z)V$, where
$$H_p=\begin{pmatrix}H_0^{(0)}&H_0^{(1)}\\ H_1^{(0)}&H_1^{(1)}\end{pmatrix},\qquad F_p=\begin{pmatrix}F_0^{(0)}&F_1^{(0)}\\ F_0^{(1)}&F_1^{(1)}\end{pmatrix},$$
and PR without delay $\iff F_p(z)H_p(z)=I$. The opposite signs of the power of $z$ in analysis and synthesis are not a mistake: this way both relations come out without extra delays. The modulation matrix $H_m(z)=\begin{pmatrix}H_0(z)&H_0(-z)\\H_1(z)&H_1(-z)\end{pmatrix}=H_p(z^2)\begin{pmatrix}1&1\\ z&-z\end{pmatrix}$, hence $\det H_m(z)=-2z\det H_p(z^2)$.

---

## 1. Discrete LTI systems [core] {#lti}

### 1.1. Convolution: linear, circular, correlation {#lti-convolution}

**Purpose.** The basic operation of every filter bank.

- Linear: $y[n]=\sum_k h[k]x[n-k]$; lengths $N$ and $L$ give $N+L-1$.
- Circular of length $N$: $(h\circledast x)[n]=\sum_{k}h[k]\,x[(n-k)\bmod N]$ — linear convolution "wrapped" in time. It coincides with the linear one if the signals are zero-padded to a length $\ge N_x+L-1$.
- Correlation: $r[n]=\sum_k h[k]x[k+n]=(h^-*x)[n]$. The analysis of an orthogonal bank is correlation with `rec_lo`, that is, convolution with `dec_lo`.

**Algorithms and complexity.** Direct — $O(NL)$; via FFT — $O((N+L)\log(N+L))$; overlap-add/overlap-save with a block $B\approx 2..4L$ — $O(N\log L)$. The FFT break-even point for double is usually $L\sim 32..128$ (measure it on the target machine). Wavelet filters ($L\le 30$) are computed directly or by lifting; FFT is needed for the CWT and for long FIR filters.

**Traps.** The start offset (the Laurent offset); confusing circular and linear convolution; FFT convolution has an error of order $u\log N\,\|h\|\|x\|$ — absolute, not relative to each sample, so small outputs get a large relative error. Compare the reconstruction error with $\|x\|$, not element by element.

**Check.** Direct convolution = FFT convolution = product with a Toeplitz matrix (§2.2) to within $10^{-13}\|h\|_1\|x\|_\infty$; circular = linear after wrapping modulo $N$.

### 1.2. z-transform and DTFT {#lti-z-dtft}

Properties: $h*x\leftrightarrow HX$; $x[n-k]\leftrightarrow z^{-k}X$; $x[-n]\leftrightarrow X(z^{-1})$; $(-1)^nx[n]\leftrightarrow X(-z)$; $a^nx[n]\leftrightarrow X(z/a)$. The region of convergence of an FIR filter is the whole plane except $0$ (and except $\infty$ for negative indices). The DTFT is $X(z)$ on the unit circle, with period $2\pi$; Parseval: $\sum|x|^2=\frac1{2\pi}\int_{-\pi}^{\pi}|X|^2d\omega$. The DFT samples the DTFT at $\omega_k=2\pi k/N$; zero-padding makes the grid denser but does not increase the resolution.

**Check.** The DTFT by direct summation (Horner) and via zero-padded FFT agree to within $10^{-14}\|h\|_1$; numerical Parseval.

### 1.3. Frequency response, phase, delays {#lti-frequency-response}

$H(e^{i\omega})=|H|e^{i\varphi(\omega)}$; magnitude response $|H|$, phase response $\varphi$, group delay $\tau_g=-d\varphi/d\omega$, phase delay $\tau_p=-\varphi/\omega$. In detail, with numerically stable formulas — §8.

### 1.4. Linear phase, types I–IV {#lti-linear-phase}

A real $h$ of length $L$, $M=L-1$. Symmetric: $h[n]=h[M-n]$, then $H(e^{i\omega})=e^{-i\omega M/2}A(\omega)$; antisymmetric: $h[n]=-h[M-n]$, $H=ie^{-i\omega M/2}A(\omega)$; $A$ is real (it may change sign).

| type | symmetry | $L$ | $A(\omega)$ | forced zeros |
|---|---|---|---|---|
| I | symm. | odd | $\sum a_k\cos k\omega$ | none |
| II | symm. | even | $\sum b_k\cos(k-\tfrac12)\omega$ | $z=-1$ |
| III | antisymm. | odd | $\sum c_k\sin k\omega$ | $z=\pm1$ |
| IV | antisymm. | even | $\sum d_k\sin(k-\tfrac12)\omega$ | $z=1$ |

The zeros of a linear-phase filter come in pairs $(z_0,1/z_0)$, and together with conjugates in quadruples; zeros on the circle come in conjugate pairs; $\pm1$ singly. The group delay is constant and equals $M/2$ (a half-integer for even $L$).

Consequence for banks: if $f_0$ is of type II, then $h_1[n]=(-1)^nf_0[n]$ is antisymmetric (type IV); if $f_0$ is of type I, then $h_1$ is of type I. Biorthogonal linear-phase banks are "odd/odd" pairs (5/3, 9/7) or "even/even" pairs (2/6, 4/4). **Theorem:** the only real FIR orthogonal two-channel bank with linear phase is Haar (up to trivial transformations). The choice is either orthogonality or symmetry.

**Check.** For a symmetric filter the group delay on the grid equals $M/2$ away from the zeros; the roots come in reciprocal pairs (after removing the known zeros at $\pm1$).

---

## 2. Toeplitz matrices [core / v1 / v2] {#toeplitz}

### 2.1. Definitions and the symbol {#toeplitz-definitions}

$T_{ij}=t_{i-j}$ — an $n\times n$ matrix is defined by $2n-1$ numbers; the symbol is $a(\omega)=\sum_k t_ke^{ik\omega}$, $T=T_n(a)$. Hankel matrix: $H_{ij}=h_{i+j}$. The exchange matrix $J$ (ones on the antidiagonal): $JTJ=T^{\mathsf T}$ (persymmetry).

### 2.2. Convolution as a banded Toeplitz matrix [core] {#toeplitz-convolution}

**Formulas.** $y=T_hx$, $T_h\in\mathbb R^{(N+L-1)\times N}$, $(T_h)_{ij}=h[i-j]$: a lower-triangular banded matrix of bandwidth $L$. The `same`/`valid` modes are subsets of the rows. For full rectangular matrices $T_gT_h=T_{g*h}$ exactly; square truncations are not closed under multiplication — the corrections are low-rank, of Hankel form, concentrated near the edges.

**Algorithm.** The matrix is not stored: multiplication is direct convolution, $O(NL)$. Explicit matrices are built only for Explain mode and for tests ($N\le 1024$).

**Check.** $T_hx$ (dense) = convolution; $T_gT_h=T_{g*h}$ for full matrices.

### 2.3. Circulants and DFT diagonalization [core] {#toeplitz-circulant}

**Formulas.** $C=\mathrm{circ}(c)$, $C_{ij}=c[(i-j)\bmod N]$. Then $C=F^{-1}\operatorname{diag}(Fc)\,F$: the eigenvalues are $\lambda_k=\hat c[k]$ (the DFT of the first column), the eigenvectors are the Fourier harmonics. Consequences: circular convolution is element-wise multiplication of spectra, $O(N\log N)$; solving $Cx=b$ is division of spectra (if $\hat c[k]\ne0$); all circulants commute; $\|C\|_2=\max|\hat c|$, $\mathrm{cond}(C)=\max|\hat c|/\min|\hat c|$. The DWT `periodization` mode yields block-circulant matrices (§6.4).

**Check.** The eigenvalues of the dense $C$ (sorted) coincide with FFT($c$); $Cx$ = circular convolution.

### 2.4. Toeplitz plus Hankel: symmetric extension [v1] {#toeplitz-plus-hankel}

**Formulas.** Half-sample symmetric extension (HS, `symmetric` in pywt): $\tilde x[-1-n]=x[n]$, $\tilde x[N+n]=x[N-1-n]$. The convolution restricted to $[0,N)$, for $L<N$:
$$y=(T+H_L+H_R)x,\quad T_{ij}=h[i-j],\ (H_L)_{ij}=h[i+j+1],\ (H_R)_{ij}=h[i+j+1-2N].$$
Whole-sample (WS, `reflect`): $\tilde x[-n]=x[n]$; $(H_L)_{ij}=h[i+j]$ for $j\ge1$, $(H_R)_{ij}=h[i+j-2N+2]$ for $j\le N-2$. For long filters the reflections repeat: the extension is periodic with period $2N$ (HS) or $2N-2$ (WS).

**Relation to the DCT.** For a zero-phase symmetric filter ($h[k]=h[-k]$) and HS extension, $C_{\mathrm{II}}(T+H_L+H_R)C_{\mathrm{II}}^{\mathsf T}=\operatorname{diag}\big(H(e^{i\pi k/N})\big)$, where $C_{\mathrm{II}}$ is the orthonormal DCT-II (verified numerically, $N=10$). WS corresponds to DCT-I. This is the analogue of the "circulant ↔ DFT" pair for symmetric boundaries: fast filtering with symmetric edges and the exact spectrum of the matrix.

**Check.** $(T+H_L+H_R)x$ = convolution of the explicitly extended signal; DCT-II diagonalization to within $10^{-13}$.

### 2.5. Fast multiplication via circulant embedding [v1] {#toeplitz-fast-matvec}

**Algorithm.** An $n\times n$ matrix $T$ with first column $c$ and first row $r$ is embedded in a circulant of order $m\ge2n-1$ with first column $[c_0,\dots,c_{n-1},0,\dots,0,r_{n-1},\dots,r_1]$; $Tx$ is the first $n$ components of $C[x;0]$. Cost $O(m\log m)$; choose $m$ "smooth" ($2^a3^b5^c$). Rectangular convolution matrices are handled the same way.

**Check.** Agreement with dense multiplication for random $n$, including primes.

### 2.6. Toeplitz systems: Levinson–Durbin, Schur [v2] {#toeplitz-levinson}

**Role in the project.** Least-squares FIR design (the normal equations are Toeplitz or "Toeplitz + Hankel", §9.5), LPC/AR envelopes as features, the Wiener filter for comparison with wavelet denoising, spectral factorization by Bauer's method (§4.7).

**Durbin** (symmetric positive definite $T$, Yule–Walker equations $T_ma^{(m)}=-[r_1..r_m]^{\mathsf T}$, $A(z)=1+\sum a_jz^{-j}$): $E_0=r_0$,
$$k_m=-\frac{r_m+\sum_{j=1}^{m-1}a^{(m-1)}_jr_{m-j}}{E_{m-1}},\quad a^{(m)}=\begin{pmatrix}a^{(m-1)}\\0\end{pmatrix}+k_m\begin{pmatrix}Ja^{(m-1)}\\1\end{pmatrix},\quad E_m=(1-k_m^2)E_{m-1}.$$
$T$ is positive definite $\iff |k_m|<1$ for all $m$. An arbitrary right-hand side calls for the Levinson algorithm; both take $O(n^2)$ operations and $O(n)$ memory. Weakly stable for SPD; a zero leading minor (indefinite matrices) causes a breakdown: look-ahead or dense LU with pivoting is needed.

**Schur** computes the same reflection coefficients and $LDL^{\mathsf T}$ without inner products — it parallelizes better; Bauer's method is also done with it. Superfast $O(n\log^2n)$ algorithms are [research], not needed for our sizes.

**Check.** Solution = dense solution; $|k_m|<1$ on AR spectra; breakdown detection on a matrix with a singular leading minor.

### 2.7. The Gohberg–Semencul formula [v2] {#toeplitz-gohberg-semencul}

Let $T$ be an invertible $n\times n$ Toeplitz matrix, $x=T^{-1}e_1$, $y=T^{-1}e_n$, $x_0\ne0$. Then
$$T^{-1}=\frac1{x_0}\Big(\mathcal L(x)\,\mathcal L(Jy)^{\mathsf T}-\mathcal L(Zy)\,\mathcal L(ZJx)^{\mathsf T}\Big),$$
where $\mathcal L(v)$ is the lower-triangular Toeplitz matrix with first column $v$, $Z$ is the down-shift, $J$ is the exchange matrix (verified on a random nonsymmetric $T$, $n=6$). Consequence: once $x,y$ are computed (Levinson, $O(n^2)$), each solution $T^{-1}b$ costs four triangular Toeplitz multiplications via FFT, $O(n\log n)$. Useful for repeated solves with the same $T$.

### 2.8. The spectral view (symbol) [v1] {#toeplitz-symbol}

For a real even bounded symbol $a$ the eigenvalues of $T_n(a)$ lie in $[\min a,\max a]$ and are distributed like the values of $a$ (Szegő): $\frac1n\sum_jF(\lambda^{(n)}_j)\to\frac1{2\pi}\int F(a(\omega))d\omega$; for $a>0$, $\mathrm{cond}(T_n)\uparrow\max a/\min a$. The block analogue for a filter bank: the singular values of the analysis matrix fill $\big[\min_\omega\sigma_{\min}(H_p(e^{i\omega})),\max_\omega\sigma_{\max}(H_p(e^{i\omega}))\big]$ — these are the Riesz bounds of the bank. In periodization mode this holds exactly (§6.4).

**Check.** The dense eigenvalues of $T_n(a)$ lie within $[\min a,\max a]$; the singular values of the analysis matrix coincide with the union of the singular values of $H_p$ on the grid (§6.4).

---

## 3. Multirate processing and resampling [core / v1] {#multirate}

### 3.1. Downsampling and upsampling: aliasing and imaging [core] {#multirate-down-up}

**Formulas.** $\downarrow M$: $y[n]=x[Mn]$,
$$Y(z)=\frac1M\sum_{k=0}^{M-1}X\big(z^{1/M}W_M^k\big),\quad W_M=e^{-2\pi i/M};\qquad Y(e^{i\omega})=\frac1M\sum_{k=0}^{M-1}X\big(e^{i(\omega-2\pi k)/M}\big).$$
The spectrum is stretched by a factor of $M$; the $M$ copies overlap — **aliasing** — unless $X$ is band-limited to $|\omega|<\pi/M$. For $M=2$: $Y(z)=\tfrac12[X(z^{1/2})+X(-z^{1/2})]$.

$\uparrow M$: $y[n]=x[n/M]$ for $M\mid n$, otherwise $0$; $Y(z)=X(z^M)$ — the spectrum is compressed, and $M-1$ **images (imaging)** appear at $2\pi k/M$.

The "down-up" pair: $(\uparrow2)(\downarrow2)x=\tfrac12\big(x+(-1)^nx\big)\leftrightarrow\tfrac12[X(z)+X(-z)]$ — the aliasing component $X(-z)$, present in every two-channel bank.

The "compression/stretching" of the old code is exactly $\downarrow2/\uparrow2$; without filters it is not a change of the sound's tempo but aliasing and imaging.

**Check.** A sinusoid of frequency $\omega_0>\pi/M$ appears after $\downarrow M$ at $|\omega_0M-2\pi k|$; after $\uparrow M$ the spectrum contains copies at $\pm\omega_0/M+2\pi k/M$.

### 3.2. Decimator and interpolator for arbitrary M [core] {#multirate-anti-alias}

- **Decimation:** a lowpass with cutoff $\pi/M$ and gain $1$ in the passband, then $\downarrow M$.
- **Interpolation:** $\uparrow M$, then a lowpass with cutoff $\pi/M$ and **gain $M$**: inserting zeros reduces the mean (DC) by a factor of $M$, and the filter restores the amplitude.

A practical audio specification: passband up to 20 kHz, stopband from half the new sampling rate, attenuation 90–120 dB. In a two-channel bank, by contrast, aliasing in each channel is allowed because the synthesis cancels it (§4.2) — a different philosophy from the decimator's.

**Check.** The residual aliasing (a tone in the stopband) is below the specification; the amplitude error of a passband tone is within the ripple.

### 3.3. Noble identities [core] {#multirate-noble}

$(\downarrow M)\to G(z)\ \equiv\ G(z^M)\to(\downarrow M)$; $\ G(z)\to(\uparrow M)\ \equiv\ (\uparrow M)\to G(z^M)$. The operations $\uparrow L$ and $\downarrow M$ commute if and only if $\gcd(L,M)=1$. Consequence: a multilevel tree reduces to single-stage equivalent filters (§8.7).

**Check.** Both sides of the identity agree element-wise on a random signal.

### 3.4. Polyphase decomposition for M channels [core] {#multirate-polyphase}

Type 1: $H(z)=\sum_{p=0}^{M-1}z^{-p}E_p(z^M)$, $e_p[n]=h[nM+p]$. A decimator in polyphase form computes only the samples that are kept: $L$ multiplications per output sample instead of $LM$. Interpolator (type 2, commutator): output phase $p$ is produced by the filter $E_p$ at the low rate. The two-channel notation of §0.5 is the variant with $z^{+1}$ in the analysis.

### 3.5. Rational resampling L/M [v1] {#multirate-rational}

**Scheme.** $\uparrow L\to H\to\downarrow M$, where $H$ is a lowpass with cutoff $\min(\pi/L,\pi/M)$ and gain $L$.

**Polyphase implementation.** $u=\uparrow L\,x$, $w=h*u$, $y[m]=w[mM]=\sum_r h[mM-rL]x[r]$. With $q_m=\lfloor mM/L\rfloor$, $p_m=mM\bmod L$:
$$y[m]=\sum_j h[jL+p_m]\,x[q_m-j].$$
The $L$ subfilters $e_p[j]=h[jL+p]$ of length $\lceil N_h/L\rceil$ are built in advance; the cost is $\lceil N_h/L\rceil$ multiply-accumulates (MAC) per output sample, with no access to zeros or to discarded samples; the phases repeat with a period of $L$ outputs ($M$ inputs).

**Example 44.1 → 48 kHz.** $48000/44100=160/147$; the intermediate rate is $7.056$ MHz. Passband 20 kHz, stopband from 22.05 kHz, 100 dB: by Kaiser (§9.3) $N_h\approx(A-8)/(2.285\,\Delta\omega)$, $\Delta\omega=2\pi\cdot2050/7.056\cdot10^6$, hence $N_h\approx2.2\cdot10^4$ taps, $\approx138$ MAC per output sample and channel ($\approx6.6\cdot10^6$ MAC/s) — verified. Coefficient table: 160 phases × 138 taps. The naive scheme is 160 times more expensive.

**Arbitrary and variable ratio** — a polyphase table with interpolation between phases (band-limited interpolation, the Farrow structure) [v2].

**Check.** Sinusoid: frequency and amplitude after resampling; the level of aliasing and images (a tone near 21 kHz); $L/L$ is the identity; the chain 44.1→48→44.1 has an error within the passband ripple; comparison with a reference (`scipy.signal.resample_poly` with the same filter).

### 3.6. Multistage decimation [v1] {#multirate-multistage}

For large $M=M_1M_2\cdots$ the first stages get a wide transition band (it is enough to suppress aliasing only in the final band), while the steepest filter runs at the lowest rate, in the last stage. Halfband stages ($M=2$) have every other coefficient equal to zero. The saving for $M\ge8..16$ is usually an order of magnitude in cost (Crochiere–Rabiner). The Mallat pyramid is the same multistage structure built of $\downarrow2$ stages, but without the requirement to suppress aliasing.

### 3.7. Polyphase and modulation matrices for M channels [v2] {#multirate-matrices}

Analysis $H_k(z)=\sum_pE_{k,p}(z^M)z^{-p}$ gives the $M\times M$ polyphase matrix $E(z)$; the modulation matrix is $[H_m(z)]_{k,l}=H_k(zW_M^l)$. The bank output: $\hat X(z)=\frac1M\sum_{l}\Big(\sum_kF_k(z)H_k(zW^l)\Big)X(zW^l)$. For $M=2$ in our notation $H_m(z)=H_p(z^2)\begin{pmatrix}1&1\\z&-z\end{pmatrix}$.

### 3.8. Time stretching without pitch change is a different operation [v2] {#multirate-time-stretch}

Resampling changes both the duration and the pitch (like playback at a different speed). The tempo can be changed without changing the pitch only through analysis–modification–synthesis: the **phase vocoder** (STFT with different analysis and synthesis hops and recomputed phases; the "phasiness" artifact is cured by phase locking — Laroche–Dolson 1999) or **WSOLA** (in the time domain: overlap-add of segments chosen by waveform similarity — Verhelst–Roelands 1993). This is not a multirate filter-bank operation: every frame is synthesised anew.

The vocoder takes output frame $j$ from the input point $t_j = j\,r$ ($r$ is how many times faster), interpolates the magnitudes of the neighbouring frames and accumulates their phase difference, and synthesises with the same hop $R$ — the duration divides by $r$ while the period inside a frame stays:

$$\varphi(j,k) = \varphi(j-1,k) + \arg X(i_{j-1}+1,k) - \arg X(i_{j-1},k), \qquad i_j = \lfloor t_j \rfloor$$

A pitch shift by $n$ semitones is a stretch with $r = 2^{-n/12}$ followed by resampling by $r$: the duration returns and every frequency is multiplied by $2^{n/12}$. In the program these are the blocks `time_stretch`, `pitch_shift` and `speed` and the view «Spectrum ▸ Tone and time».

### 3.9. Fundamental frequency: YIN [v2] {#pitch-yin}

The pitch of a periodic sound is set by its period $T_0 = 1/f_0$. Autocorrelation looks for it as the maximum of $r(\tau)$ but easily errs by an octave: the peak at $2T_0$ is no lower. YIN (de Cheveigné–Kawahara 2002) takes the difference function of a frame of length $W$ and normalises it by its mean over smaller lags:

$$d(\tau) = \sum_{j=0}^{W-1} \big(x[j] - x[j+\tau]\big)^2, \qquad d'(\tau) = \frac{\tau\, d(\tau)}{\sum_{s=1}^{\tau} d(s)}, \quad d'(0) = 1$$

The period is the first $\tau$ where $d'(\tau)$ drops below the threshold $\theta$ (usually 0.1–0.15), moved to the nearest minimum and refined by a parabola through three points; $f_0 = f_s/\hat\tau$. If $d'$ never drops below the threshold the frame is unvoiced. Lags are limited to $[f_s/f_{\max},\ f_s/f_{\min}]$, and the frame must hold the window $W$ and $\tau_{\max}$.

---

## 4. Two-channel filter banks [core] {#two-channel}

### 4.1. Distortion and aliasing equations {#fb-distortion-alias}

**Formulas.** With $v_i=\downarrow2(h_i*x)$ and $\hat x=\sum f_i*\uparrow2\,v_i$:
$$\hat X(z)=T(z)X(z)+A(z)X(-z),\quad T=\tfrac12\big[F_0H_0+F_1H_1\big],\quad A=\tfrac12\big[F_0(z)H_0(-z)+F_1(z)H_1(-z)\big],$$
in matrix form $[T\ \ A]=\tfrac12[F_0\ \ F_1]H_m(z)$. **PR with delay $\ell$:** $A\equiv0$, $T=cz^{-\ell}$.

**Check.** Compute $T$ and $A$ as Laurent polynomials and compare them with $z^{-\ell}$ and $0$ exactly (rational filters) or to within $10^{-15}$.

### 4.2. Choosing the synthesis to cancel aliasing {#fb-alias-cancellation}

The general solution: $[F_0\ F_1]=[2cz^{-\ell},\,0]\,H_m^{-1}$, that is,
$$F_0=\frac{2cz^{-\ell}}{D(z)}H_1(-z),\quad F_1=-\frac{2cz^{-\ell}}{D(z)}H_0(-z),\quad D=\det H_m=H_0(z)H_1(-z)-H_0(-z)H_1(z).$$
$D(-z)=-D(z)$; an FIR synthesis for an FIR analysis exists $\iff D$ is a monomial of odd degree.

**The standard choice** (Strang–Nguyen, old code): $H_1(z)=F_0(-z)$, $F_1(z)=-H_0(-z)$. Then $A\equiv0$ for any $H_0,F_0$, and
$$T=\tfrac12\big[P_0(z)-P_0(-z)\big],\quad P_0=F_0H_0 .$$
**PR $\iff P_0(z)-P_0(-z)=2z^{-\ell}$**: $P_0$ has a single nonzero odd coefficient $p_0[\ell]=1$, $\ell$ is odd, and the even coefficients are free. The zero-phase form $P(z)=z^{\ell}P_0(z)$ is a **halfband** filter, $P(z)+P(-z)=2$ ($p[0]=1$, $p[2k]=0$ for $k\ne0$). Designing a bank = choosing a halfband $P_0$ and factorizing it as $P_0=F_0H_0$ (§4.6–4.8).

### 4.3. QMF (Croisier–Esteban–Galand) {#fb-qmf}

$H_1(z)=H_0(-z)$, $F_0=H_0$, $F_1=-H_1$: aliasing is cancelled, $T=\tfrac12[H_0^2(z)-H_0^2(-z)]=2z^{-1}E_0(z^2)E_1(z^2)$ for $H_0=E_0(z^2)+z^{-1}E_1(z^2)$. FIR PR is possible only for monomial $E_0,E_1$, that is, for the two-tap (Haar) filter. Practical QMFs (Johnston) are linear-phase with near-perfect reconstruction. The "mirror" property: $|H_1(e^{i\omega})|=|H_0(e^{i(\pi-\omega)})|$ — symmetry about $\pi/2$. In the project it serves only as a historical and teaching example.

### 4.4. CQF / orthogonal bank (Smith–Barnwell) {#fb-cqf}

$L$ is even, $N=L-1$: $H_1(z)=-z^{-N}H_0(-z^{-1})$, $F_0(z)=z^{-N}H_0(z^{-1})$, $F_1(z)=z^{-N}H_1(z^{-1})$ (the synthesis is the reversed analysis). This is a special case of §4.2: $H_1(z)=F_0(-z)$ for odd $N$. $P_0=z^{-N}H_0(z)H_0(z^{-1})$, and PR (normalization $\sqrt2$) is equivalent to each of the conditions:
$$\textstyle\sum_nh_0[n]h_0[n+2k]=\delta_k\ \iff\ |H_0(e^{i\omega})|^2+|H_0(e^{i(\omega+\pi)})|^2=2\ \iff\ R(z)+R(-z)=2,\ R=H_0(z)H_0(z^{-1}).$$
Since $R=|H_0|^2\ge0$ on the circle, the halfband $P$ for an orthogonal bank must be **nonnegative on the circle** (its zeros there have even multiplicity).

**Check.** The double-shift orthogonality relation to $10^{-15}$; power symmetry on a 4096-point grid.

### 4.5. The polyphase PR condition and paraunitarity {#fb-polyphase-pr}

**Formulas** (notation of §0.5). PR without delay $\iff F_pH_p=I$; with delay $\ell=2m$: $F_pH_p=z^{-m}I$; with $\ell=2m+1$: $F_pH_p=\begin{pmatrix}0&z^{-m-1}\\z^{-m}&0\end{pmatrix}$. An FIR inverse exists $\iff\det H_p(z)=cz^{-k}$, $c\neq0$. Then $F_p=\operatorname{adj}H_p/\det H_p$:
$$F_0^{(0)}=\frac{H_1^{(1)}}{\det},\ F_1^{(0)}=-\frac{H_0^{(1)}}{\det},\ F_0^{(1)}=-\frac{H_1^{(0)}}{\det},\ F_1^{(1)}=\frac{H_0^{(0)}}{\det},$$
which in time-domain form gives $F_0(z)=-z^{-1}H_1(-z)/\det H_p(z^2)$, $F_1(z)=z^{-1}H_0(-z)/\det H_p(z^2)$ — the same aliasing-cancellation structure.

**Paraunitarity:** $\tilde H_p(z)H_p(z)=I$, $\tilde H(z)=H^{\mathsf T}(z^{-1})$ (real coefficients), that is, $H_p(e^{i\omega})$ is unitary for all $\omega$; then $F_p=\tilde H_p$, the bank is orthonormal and Parseval holds. Every $2\times2$ FIR paraunitary matrix factors into a lattice $R_K\Lambda(z)R_{K-1}\cdots\Lambda(z)R_0$, $\Lambda=\operatorname{diag}(1,z^{-1})$, where the $R_k$ are rotations (Vaidyanathan): PR is preserved under any quantization of the angles [v2].

**Check.** $\det H_p$ is a monomial (as a Laurent polynomial); the product $F_pH_p$ as a polynomial matrix equals $I$; for orthogonal banks $H_p(e^{i\omega})$ is unitary on the grid.

### 4.6. The Daubechies maximally flat halfband filter {#fb-maxflat}

**Formulas.** With $y=\sin^2(\omega/2)=(2-z-z^{-1})/4$ and $\cos^2(\omega/2)=(1+z)(1+z^{-1})/4$:
$$P_p(z)=2\left(\frac{(1+z)(1+z^{-1})}4\right)^{p}B_p\!\left(\frac{2-z-z^{-1}}4\right),\qquad B_p(y)=\sum_{k=0}^{p-1}\binom{p-1+k}{k}y^k .$$
The Bézout identity $(1-y)^pB_p(y)+y^pB_p(1-y)=1$ gives $P_p(z)+P_p(-z)=2$; $B_p$ is the unique polynomial of minimal degree with this property. The causal form $P_0(z)=z^{-(2p-1)}P_p(z)$: length $4p-1$, delay $\ell=2p-1$, $2p$ zeros at $z=-1$, maximal flatness at $\omega=0$ and $\omega=\pi$ (the FIR analogue of the Butterworth filter; the closed form is due to Herrmann 1971).

**Example $p=2$.** $B_2=1+2y$, $P_0=\frac1{16}(-1+9z^{-2}+16z^{-3}+9z^{-4}-z^{-6})=-\frac1{16}(1+z^{-1})^4(1-4z^{-1}+z^{-2})$, $\ell=3$ (verified). The root $y=-\tfrac12$ gives $z+z^{-1}=4$, $z=2\pm\sqrt3$.

**Roots.** Each root $y_j$ of $B_p$ (degree $p-1$) gives a pair of reciprocal roots: $z^2-c_jz+1=0$, $c_j=2-4y_j$, $z=(c_j\pm\sqrt{c_j^2-4})/2$. $B_p>0$ on $[0,1]$, so these roots do not lie on the circle. The roots are sought in the variable $y$ (degree $p-1$), not in $z$ (degree $4p-2$); the zeros at $-1$ are never passed to the root finder.

**Check.** Compare $P_p$ with the formula for $p=1..20$ in rational arithmetic (the coefficients of $B_p$ are integers); check $P_p(z)+P_p(-z)=2$ exactly.

### 4.7. Spectral factorization: root assignment rules {#fb-spectral-factorisation}

The roots of $P$ ($2p$ at $z=-1$ and $2(p-1)$ off the circle) are split between $H_0$ and $F_0$.

- **Orthogonal case (dbN, $N=p$):** we need $F_0=$ the reversal of $H_0$; from each reciprocal pair $\{z_j,1/z_j\}$ (conjugates together) one root is taken, and each filter gets $p$ zeros at $-1$. Minimum phase means all $|z_j|<1$: this is dbN (`rec_lo` in pywt). There are $2^{n_r+n_c}$ variants ($n_r$ real pairs, $n_c$ complex quadruples). **Symlets** are the variant with the phase closest to linear (Daubechies' criterion; exhaustive search is feasible for $p\le20$; in doubtful cases check against the pywt tables).
- **Biorthogonal linear-phase:** each reciprocal pair (quadruple) goes entirely into one filter; $k$ zeros at $-1$ go to $H_0$, $2p-k$ to $F_0$. The CDF spline family: $F_0\propto(1+z^{-1})^{N_r}$ (a B-spline), everything else goes to $H_0$. **CDF 9/7** ($p=4$): $B_4$ has one real root (a real pair → the 7-tap synthesis filter) and a pair of complex ones (a complex quadruple → the 9-tap analysis lowpass), and each filter gets 4 zeros at $-1$. Verified against `bior4.4`: zeros of `dec_lo` $2.031\pm1.739i$, $0.284\pm0.243i$; zeros of `rec_lo` $3.041$, $0.329$.
- **Non-maximally-flat $P$** (Remez, §9.6) lifted to $P\ge0$ for orthogonal banks — the Smith–Barnwell procedure [v2].

**Numerical difficulties.** A zero of multiplicity $m$ found numerically has an error $\sim u^{1/m}$: for a 9-tap filter with a fourfold zero at $-1$ the computed roots deviate by $1.2\cdot10^{-4}$ (verified). Rule: the zeros at $-1$ are known exactly and are introduced as the factor $(1+z^{-1})^k$ with binomial coefficients. The condition number of the roots of $B_p$ grows exponentially with $p$; pywt stores db up to db38 as precomputed tables. Alternatives:

1. Extended precision (MPFR / Boost.Multiprecision, 100–300 bits) offline, then tables.
2. **Bauer's method:** the Cholesky factorization of the banded Toeplitz $T_n(Q)$ with symbol $Q(e^{i\omega})>0$; the rows of the factor converge to the coefficients of the minimum-phase factor. Convergence is slow when the symbol has zeros on the circle — so $(1+z^{-1})^{2p}$ is factored out first, the remainder $Q$ is factorized, and the factor is put back.
3. **Cepstral (Kolmogorov):** $\log Q(e^{i\omega})\to$ IFFT $\to$ keep $c[0]/2$ and $c[n>0]$ $\to$ FFT $\to\exp$; requires $Q>0$ on the circle (again, factor out the zeros at $-1$) and a large FFT ($\ge32\times$ the filter length) because of cepstral aliasing.

**Check.** The product of the factors reproduces $P$ to within $10^{-14}\|P\|$; the halfband property; orthogonality $\sum h[n]h[n+2k]=\delta_k$; minimum phase — all zeros (except $-1$) inside the unit circle; agreement with the pywt tables.

### 4.8. All factorizations of P0 of degree 6 (the Strang–Nguyen example) {#fb-p0-factorisations}

Notation: $r=2-\sqrt3\approx0.268$, $1/r=2+\sqrt3$; $H_0=c\,(1+z^{-1})^k\prod_{s\in S}(1-sz^{-1})$, $S\subseteq\{r,1/r\}$, $F_0$ takes the remaining roots; normalization $H_0(1)=1$, $F_0(1)=2$. All 20 factorizations give PR by construction. For the bank to generate a **Riesz wavelet basis**, both functions $\varphi$ (from $F_0$) and $\tilde\varphi$ (from $H_0$) must lie in $L^2$, that is, the Sobolev exponent must satisfy $s>0$. It can be computed exactly: for $m(\omega)=\big(\tfrac{1+e^{-i\omega}}2\big)^NL(\omega)$, $m(0)=1$,
$$s=N-\log_4\rho(T_W),\quad (T_W)_{n,k}=2w_{2n-k},\ n,k\in[-K,K],$$
where $w_j$ are the coefficients of $|L(\omega)|^2=\sum w_je^{ij\omega}$, $K=\deg L$ (Villemoes 1992). The formula is checked against known values: Haar $0.5$, the "hat" $1.5$, db2 $1.0$, the cubic B-spline $3.5$. The condition number $\kappa=\max_\omega\sigma_{\max}/\min_\omega\sigma_{\min}$ of the polyphase matrix is taken with normalization $\sqrt2$ for both lowpass filters. Mirror variants ($r\leftrightarrow1/r$, time reversal) have the same properties and are merged.

| $L_{H_0}/L_{F_0}$ | zeros at $-1$: $H_0/F_0$ | $S$ in $H_0$ | lin. phase | $H_0$ (norm. $H_0(1)=1$) | $F_0$ (norm. $F_0(1)=2$) | $s(\tilde\varphi)$ | $s(\varphi)$ | $\kappa$ | verdict |
|---|---|---|---|---|---|---|---|---|---|
| 1/7 | 0/4 | — | yes | $1$ | $(-1,0,9,16,9,0,-1)/16$ | — | 2.44 | 2.62 | no: $\tilde\varphi=\delta$ |
| 2/6 ×2 | 0/4 | $\{r\}$ | no | $\approx(1.366,-0.366)$ | 6 taps | — | 3.00 | 4.79 | no: $H_0$ has no zero at $-1$ |
| 3/5 | 0/4 | $\{r,1/r\}$ | yes | $(-1,4,-1)/2$ | $(1,4,6,4,1)/8$ | — | 3.50 | 10.9 | no |
| **2/6** | 1/3 | — | yes | $(1,1)/2$ | $(-1,1,8,8,1,-1)/8$ | 0.50 | 1.44 | 1.28 | **yes**: `rbio1.3` |
| 3/5 ×2 | 1/3 | $\{r\}$ | no | $\approx(0.683,0.5,-0.183)$ | 5 taps | 0.00 | 2.00 | 2.00 | no: $s=0$, $\tilde\varphi\notin L^2$ |
| 4/4 | 1/3 | $\{r,1/r\}$ | yes | $(-1,3,3,-1)/4$ | $(1,3,3,1)/4$ | −0.56 | 2.50 | 4.00 | no: `bior3.1`, $\tilde\varphi\notin L^2$ |
| **3/5** | 2/2 | — | yes | $(1,2,1)/4$ | $(-1,2,6,2,-1)/4$ | 1.50 | 0.44 | 2.00 | **yes**: reversed 5/3, `rbio2.2` |
| **4/4 ×2** | 2/2 | $\{r\}$ | no | $(1{+}\sqrt3,3{+}\sqrt3,3{-}\sqrt3,1{-}\sqrt3)/8$ | $2\times$ reversed $H_0$ | 1.00 | 1.00 | 1.00 | **yes**: D4 = db2 |
| **5/3** | 2/2 | $\{r,1/r\}$ | yes | $(-1,2,6,2,-1)/8$ | $(1,2,1)/2$ | 0.44 | 1.50 | 2.00 | **yes**: LeGall 5/3 = CDF(2,2) = `bior2.2` |
| 4/4 | 3/1 | — | yes | $(1,3,3,1)/8$ | $(-1,3,3,-1)/2$ | 2.50 | −0.56 | 4.00 | no: `rbio3.1`, $\varphi\notin L^2$ |
| 5/3 ×2 | 3/1 | $\{r\}$ | no | 5 taps | $\approx(-0.366,1,1.366)$ | 2.00 | 0.00 | 2.00 | no: $s=0$ |
| **6/2** | 3/1 | $\{r,1/r\}$ | yes | $(-1,1,8,8,1,-1)/16$ | $(1,1)$ | 1.44 | 0.50 | 1.28 | **yes**: `bior1.3` |
| 5/3 | 4/0 | — | yes | $(1,4,6,4,1)/16$ | $(-1,4,-1)$ | 3.50 | — | 10.9 | no: "binomial 5/3", $F_0$ has no zero |
| 6/2 ×2 | 4/0 | $\{r\}$ | no | 6 taps | $\approx(-0.732,2.732)$ | 3.00 | — | 4.79 | no |
| 7/1 | 4/0 | $\{r,1/r\}$ | yes | $P_0/2$ | $2$ | 2.44 | — | 2.62 | no: $\varphi=\delta$ |

Conclusions (all values verified numerically):

1. A wavelet basis arises in 6 of the 20 cases: the Haar-like 2/6 and 6/2 (CDF 1,3), LeGall 5/3 in both orientations, and D4 with its mirror variant.
2. Eight factorizations give no wavelet because one side has no zero at $z=-1$: $m(\pi)\ne0$, the infinite product $\prod m(\omega/2^j)$ does not decay, and the cascade does not converge to a function. 1/7 (and 7/1) is a degenerate case: $H_0=1$ means "lazy" downsampling, and $H_1=P_0(-z)$ is the error of the 4-point Deslauriers–Dubuc prediction. This is exactly one lifting step (prediction without update, §10.6) — a useful bank, but $\tilde\varphi=\delta$.
3. Another six factorizations have zeros at $-1$ on both sides, but the side with a single zero is not regular enough ($s\le0$). In particular, **the symmetric 4/4 (CDF 3,1, `bior3.1`) is a PR bank, but $\tilde\varphi\notin L^2$**: the norm of the cascade iteration grows by a factor of $\approx2.17\approx4^{0.56}$ per step, and the condition number of the multilevel transform grows with the level — 4.0; 6.4; 10; 15; 23; 34; 50 for $J=1..7$ (versus 2.0…3.2 for `bior2.2` and 1.3…1.5 for `bior4.4`; all verified). For $s=0$ the norm grows linearly — a borderline case, also not in $L^2$.
4. The best conditioning belongs to the orthogonal D4 ($\kappa=1$); among the linear-phase banks, to 2/6 ($1.28$).

**Check.** Each table row is a test: the factorization reproduces $P_0$; $s$ by the Villemoes formula; the growth of $2^{-i}\sum c_i^2$ in the cascade iteration agrees with the sign of $s$; $\kappa$ via the SVD of the polyphase matrix on a grid of $\ge2001$ points.

---

## 5. Worked example: the Haar bank [core] {#haar}

The entry point of Explain mode: each step below is a screen with concrete numbers and the invariant that holds on it.

### 5.1. Filters and the product P0 {#haar-filters}

Strang–Nguyen signs (indices 0, 1):
$$h_0=\tfrac1{\sqrt2}[1,1],\quad h_1=\tfrac1{\sqrt2}[1,-1],\quad f_0=\tfrac1{\sqrt2}[1,1],\quad f_1=\tfrac1{\sqrt2}[-1,1].$$
In pywt `dec_hi` $=\tfrac1{\sqrt2}[-1,1]$, `rec_hi` $=\tfrac1{\sqrt2}[1,-1]$ (the highpass signs are reversed, §0.4).

$H_0=\frac{1+z^{-1}}{\sqrt2}$, $H_1=\frac{1-z^{-1}}{\sqrt2}=F_0(-z)$, $F_1=-H_0(-z)=\frac{-1+z^{-1}}{\sqrt2}$. The product $P_0=F_0H_0=\tfrac12(1+z^{-1})^2=\tfrac12(1+2z^{-1}+z^{-2})$; $P_0(z)-P_0(-z)=2z^{-1}$, delay $\ell=1$; $T=z^{-1}$, $A=\tfrac14\big[(1+z^{-1})(1-z^{-1})-(1-z^{-1})(1+z^{-1})\big]=0$. This is the maximally flat $P_p$ for $p=1$ ($B_1=1$).

### 5.2. Step-by-step example: analysis → ↓2 → ↑2 → synthesis {#haar-walkthrough}

Signal $x=[4,6,10,12,8,6,5,5]$, $N=8$.

1. **Convolutions** (length 9): $y_0=h_0*x=\tfrac1{\sqrt2}[4,10,16,22,20,14,11,10,5]$; $y_1=h_1*x=\tfrac1{\sqrt2}[4,2,4,2,-4,-2,-1,0,-5]$ ($y_1[m]=x[m]-x[m-1]$).
2. **Downsampling** — odd indices 1, 3, 5, 7 (the pywt phase): $a=\tfrac1{\sqrt2}[10,22,14,10]\approx[7.0711;\,15.5563;\,9.8995;\,7.0711]$, $d=\tfrac1{\sqrt2}[2,2,-2,0]\approx[1.4142;\,1.4142;\,-1.4142;\,0]$. Meaning: $a[k]=(x[2k]+x[2k+1])/\sqrt2$ is a scaled local mean, $d[k]=(x[2k+1]-x[2k])/\sqrt2$ is a local difference. The even phase (indices 0, 2, …, 8) would give 5+5 coefficients covering the pairs $(x[2k-1],x[2k])$ with a zero at the edge, and reconstruction with delay 1 — this is the causal Strang–Nguyen scheme.
3. **Upsampling** (onto the even positions): $u_0=[a_0,0,a_1,0,a_2,0,a_3,0]$, $u_1=[d_0,0,d_1,0,\dots]$.
4. **Synthesis filters** (samples $n=0..7$): $w_0=f_0*u_0=[5,5,11,11,7,7,5,5]$, $w_1=f_1*u_1=[-1,1,-1,1,1,-1,0,0]$.
5. **Sum:** $w_0+w_1=[4,6,10,12,8,6,5,5]=x$ — exactly and without delay (analysis with the pywt phase is equivalent to the filters $zH_i$).

Screen invariants: $\|x\|^2=446=\|a\|^2+\|d\|^2=440+6$ (Parseval); $w_0$ is the piecewise-constant mean, $w_1=x-w_0$ the details. Zeroing $d$ gives $\hat x=w_0$, the error $w_1$ and aliasing (§5.5).

**Precision.** In floating point the factors $1/\sqrt2$ introduce rounding errors. In the sum normalization ($h_0=\tfrac12[1,1]$, $f_0=[1,1]$) all intermediate values for an integer input are dyadic rationals and are computed exactly, so Explain mode can show exact fractions. A fully integer variant is the S-transform via lifting: $d=x_{\text{odd}}-x_{\text{even}}$, $s=x_{\text{even}}+\lfloor d/2\rfloor$.

### 5.3. Multilevel decomposition {#haar-multilevel}

Applying the same step to $a$: level 2 — $a_2=[16,12]$, $d_2=[6,-2]$; level 3 — $a_3=[28/\sqrt2]\approx[19.799]$, $d_3=[-4/\sqrt2]\approx[-2.828]$. The coefficient vector in pywt order $[a_3,d_3,d_2,d_1]$: $[19.799;\,-2.828;\,6;\,-2;\,1.414;\,1.414;\,-1.414;\,0]$. Energy $392+8+40+6=446$.

### 5.4. The analysis matrix for N = 8 {#haar-matrix}

One level, interleaved order $(a_0,d_0,a_1,d_1,\dots)$: $A_1=I_4\otimes B$, $B=\tfrac1{\sqrt2}\begin{pmatrix}1&1\\-1&1\end{pmatrix}$. The matrix is block-diagonal because the Haar polyphase matrix is constant, $H_p(z)=B$: the blocks do not overlap. In the order $[L;B]$: $A_1=\tfrac1{\sqrt2}\begin{pmatrix}I_4\otimes[1\ 1]\\ I_4\otimes[-1\ 1]\end{pmatrix}$. $A_1^{\mathsf T}A_1=I$, synthesis $S=A_1^{\mathsf T}$.

Three levels: $W=\operatorname{diag}(A^{(3)},I_6)\operatorname{diag}(A^{(2)},I_4)A^{(1)}$ (low frequencies first). The rows of $W$ (order $a_3,d_3,d_2,d_1$) are the discrete Haar functions:
$$\tfrac1{\sqrt8}[1,1,1,1,1,1,1,1],\ \ \tfrac1{\sqrt8}[-1,-1,-1,-1,1,1,1,1],\ \ \tfrac12[-1,-1,1,1,0,0,0,0],\ \ \tfrac12[0,0,0,0,-1,-1,1,1],\ \ \tfrac1{\sqrt2}[-1,1,0,\dots,0],\ \dots$$
$Wx$ gives the vector from §5.3, $WW^{\mathsf T}=I$. With pywt signs the detail rows change sign.

### 5.5. Magnitude response, phase response, aliasing {#haar-frequency}

$H_0(e^{i\omega})=\sqrt2\cos(\omega/2)\,e^{-i\omega/2}$, $H_1(e^{i\omega})=\sqrt2\,i\sin(\omega/2)\,e^{-i\omega/2}$. On $[0,\pi]$: $|H_0|=\sqrt2\cos(\omega/2)$, $|H_1|=\sqrt2\sin(\omega/2)$; $|H_0|^2+|H_1|^2=2$; the phase responses are $-\omega/2$ (type II) and $\pi/2-\omega/2$ (type IV), the group delay is $\tfrac12$ for both. Relative to DC ($+3.01$ dB) the $-3$ dB level falls exactly at $\pi/2$; at $3\pi/4$, $|H_0|/\sqrt2=\cos(3\pi/8)=0.383$ ($-8.3$ dB); the only zero is at $\pi$. Highpass leakage at $\pi/4$: $\sin(\pi/8)=0.383$, also $-8.3$ dB.

When the details are zeroed: $T=\tfrac12F_0H_0=\cos^2(\omega/2)e^{-i\omega}$, aliasing $|A(e^{i\omega})|=\tfrac12|F_0(e^{i\omega})H_0(e^{i(\omega+\pi)})|=\tfrac12|\sin\omega|$, with maximum $\tfrac12$ ($-6$ dB) at $\pi/2$.

### 5.6. Equivalent filters of the levels {#haar-equivalent}

The lowpass after $j$ levels is a "box" of length $2^j$: $G_j(z)=\prod_{k=0}^{j-1}H_0(z^{2^k})=2^{-j/2}\sum_{n=0}^{2^j-1}z^{-n}$. Details of level $j$: $H^{eq}_j(z)=H_1(z^{2^{j-1}})G_{j-1}(z)=2^{-j/2}\Big(\sum_{n=0}^{2^{j-1}-1}z^{-n}-\sum_{n=2^{j-1}}^{2^j-1}z^{-n}\Big)$.
$$|G_j(e^{i\omega})|=2^{-j/2}\left|\frac{\sin(2^{j-1}\omega)}{\sin(\omega/2)}\right|,\qquad |H^{eq}_j(e^{i\omega})|=2^{1-j/2}\frac{\sin^2(2^{j-2}\omega)}{|\sin(\omega/2)|}.$$
The sidelobes decay only as $1/\omega$ (the first sidelobe of the "box" is at $\approx-13$ dB) — the octave bands overlap heavily.

### 5.7. Why Haar is good in time and bad in frequency {#haar-tradeoff}

The support is exactly $2^j$ samples, shifts do not overlap (a block transform), steps and transients are localized perfectly, and the arithmetic is integer. But: there is one vanishing moment (only constants are annihilated), so a smooth signal produces noticeable details — poor energy compaction; $|H_0|\sim\cos$ — a huge transition band and strong aliasing when subbands are modified; $\psi$ is discontinuous — blocking artifacts after thresholding or zeroing.

### 5.8. Explain mode script {#haar-explain-script}

Screens: (1) the signal; (2) the filters — coefficients, magnitude response, phase response; (3) the full convolutions $y_0,y_1$; (4) downsampling with the kept samples highlighted; (5) $a,d$ and energy bars ($446=440+6$); (6) upsampling; (7) the synthesis convolutions $w_0,w_1$; (8) the sum and the zero error; (9) the matrix view $A_1x=[a;d]$, $A_1^{\mathsf T}$; (10) the multilevel tree and the matrix $W$; (11) the frequency view — $|H_0|,|H_1|,T,A$; (12) the "zero out $d$" experiment — $w_0$, the spectra of the error and of the aliasing. Each screen checks its own invariant (energy, exact reconstruction, $WW^{\mathsf T}=I$).

---

## 6. Filter bank matrices and boundary handling [core / v1] {#fb-matrices}

### 6.1. Infinite analysis and synthesis matrices {#fbm-infinite}

**Formulas** (Strang–Nguyen). $L_{k,n}=h_0[2k-n]$, $B_{k,n}=h_1[2k-n]$, $A=\begin{pmatrix}L\\B\end{pmatrix}$: each row is a reversed filter, adjacent rows are shifted by 2 (the "double shift"). Synthesis $S=[S_L\ S_B]$, $(S_L)_{n,k}=f_0[n-2k]$, $(S_B)_{n,k}=f_1[n-2k]$: the columns are shifted filters.

- PR without delay: $SA=S_LL+S_BB=I$.
- Biorthogonality: $AS=I\iff\sum_nh_i[2k-n]f_j[n-2m]=(h_i*f_j)[2(k-m)]=\delta_{ij}\delta_{km}$; for $i=j=0$ this is the halfband property of $f_0*h_0$.
- Orthogonality: $S=A^{\mathsf T}\iff f_i[n]=h_i[-n]$; $A^{\mathsf T}A=AA^{\mathsf T}=I$.

**Check.** On a $64\times64$ fragment (in the middle, away from the edges) $SA=I$ and $AS=I$.

### 6.2. Block-Toeplitz structure and the symbol H_p {#fbm-block-toeplitz}

Interleave the rows $(v_0[k],v_1[k])$ and the column pairs $(x[2n],x[2n+1])$. The block $(k,n)$
$$\begin{pmatrix}h_0[2(k-n)]&h_0[2(k-n)-1]\\ h_1[2(k-n)]&h_1[2(k-n)-1]\end{pmatrix}$$
depends only on $k-n$: the matrix is $2\times2$-block Toeplitz, and its symbol is $\sum_jB_jz^{-j}=H_p(z)$ (notation of §0.5). The synthesis has the symbol $F_p(z)$; PR $\iff F_pH_p=I$. Haar: only $B_0$ is nonzero (a block diagonal, §5.4); for length $L$ there are about $L/2$ nonzero blocks per block row.

### 6.3. Multilevel transform as a product of matrices {#fbm-multilevel}

$W_J=\operatorname{diag}(A^{(J)},I)\cdots\operatorname{diag}(A^{(2)},I)\,A^{(1)}$ (the sizes halve, the coefficient order is $[a_J,d_J,\dots,d_1]$). Orthogonal levels give an orthogonal $W_J$. The implementation does not build the matrices, except for Explain mode and tests ($N\le1024$).

### 6.4. Periodization: a block circulant [core] {#fbm-periodization}

**Formulas.** For an $N$-periodic $x$ ($N$ even), $A$ is a block circulant of $N/2$ blocks of size $2\times2$: the transform is critically sampled ($N$ coefficients), PR holds for any PR bank, and an orthogonal bank gives an orthogonal $A$ (verified with db3, $N=16$). The transform $(F_{N/2}\otimes I_2)$ block-diagonalizes $A$, with $H_p(e^{2\pi ik/(N/2)})$, $k=0..N/2-1$ on the diagonal. Therefore
$$\sigma(A)=\bigcup_k\sigma\big(H_p(e^{2\pi ik/(N/2)})\big)$$
(verified with `bior2.2`, $N=16$: $\mathrm{cond}=2.0$). This gives the exact $\mathrm{cond}(A)$ and the Riesz bounds in $O(N)$; in the infinite case, the $\inf/\sup$ over $\omega$.

**Drawback:** the discontinuity at the period seam produces large details near the edges. $J$ levels require $2^J\mid N$ (otherwise, padding).

**Check.** $\|A^{\mathsf T}A-I\|$ for orthogonal banks; Parseval; the spectrum via $H_p$ equals the dense SVD.

### 6.5. Boundary modes and output lengths [core] {#fbm-boundary-modes}

$N$ is the input length, $L$ the filter length; the length of each band after one level:

| mode (pywt) | extension | band length | critically sampled |
|---|---|---|---|
| `zero` | $0$ outside $[0,N)$ | $\lfloor(N+L-1)/2\rfloor$ | no |
| `constant` | repeat the edge sample | same | no |
| `smooth` | linear extrapolation from the two edge samples | same | no |
| `symmetric` (HS) | $\dots x_1x_0\mid x_0x_1\dots$ | same | no |
| `reflect` (WS) | $\dots x_2x_1\mid x_0x_1x_2\dots$ | same | no |
| `antisymmetric` | $\dots -x_1\,{-x_0}\mid x_0x_1\dots$ | same | no |
| `antireflect` | $\tilde x[-n]=2x_0-x_n$ | same | no |
| `periodic` | periodic, expanded | same | no |
| `periodization` | circular | $\lceil N/2\rceil$ | yes |

Verified on pywt 1.8: `idwt(dwt(x))=x` in all 9 modes for db2, `bior2.2` and db4 with $N\in\{13,16\}$; lengths $N=13,L=4\to8$, $N=16\to9$, `periodization` — 7 and 8. Multilevel lengths follow by recursion with the same formula; the pywt maximum level is $\lfloor\log_2(N/(L-1))\rfloor$.

### 6.6. Non-expansive symmetric extension [v1] {#fbm-nonexpansive-symmetric}

Critical sampling, exact PR and no discontinuity at the edge are possible at the same time only for **linear-phase** filters:

- odd length (type I: 5/3, 9/7) + WS extension (period $2N-2$): the bands are symmetric, $\lceil N/2\rceil$ low and $\lfloor N/2\rfloor$ high coefficients are stored; this is what JPEG 2000 Part 1 does;
- even length (type II lowpass, type IV highpass: 2/6) + HS extension (period $2N$): the lowpass band is HS-symmetric, the highpass band HS-antisymmetric; $N/2+N/2$ for even $N$.

In matrix terms this is a square invertible $N\times N$ matrix of "Toeplitz + Hankel" form (§2.4). Orthogonal non-Haar filters with non-expansive symmetric extension do not give PR.

**A practical implementation** is lifting (§10) with index mirroring at every step: $x[-1]:=x[1]$, $x[N]:=x[N-2]$, $d[-1]:=d[0]$. The inverse pass uses the same rule, so PR is exact by construction for any parity of $N$.

**Check.** Exactly $N$ coefficients; PR; symmetry of the extended bands; agreement with the convolution implementation on an explicitly extended signal.

### 6.7. What each mode guarantees {#fbm-mode-guarantees}

- `periodization` — a square matrix and PR for any PR bank, artifacts at the seam.
- Non-expansive symmetric — a square matrix and PR only for linear phase, no discontinuity artifacts.
- The other modes are expansive ($\approx N+L-2$ coefficients per level), achieve PR by storing all coefficients, and are compatible with pywt.

### 6.8. Cohen–Daubechies–Vial boundary wavelets [research] {#fbm-cdv}

Orthonormal bases on $[0,1]$ that keep $N$ vanishing moments near the edges: $N$ boundary scaling functions on each side, modified filters near the edges, preconditioning matrices; a square orthogonal matrix without edge artifacts; precomputed tables are needed (distributed via netlib). A simplified alternative: orthogonalization (QR) of the truncated boundary rows of $A$ — orthogonality is preserved, but there are no vanishing moments at the edges.

**Check.** $W^{\mathsf T}W=I$; the details of polynomials of degree $<N$ are zero, **including** the boundary coefficients.

---

## 7. Wavelet theory [v1] {#wavelets}

### 7.1. Multiresolution analysis (MRA) {#wav-mra}

Closed subspaces $\dots\subset V_1\subset V_0\subset V_{-1}\subset\dots$ of $L^2(\mathbb R)$ (larger $j$ is coarser, as with pywt levels): $f(t)\in V_j\iff f(2t)\in V_{j-1}$; $\bigcap V_j=\{0\}$, $\overline{\bigcup V_j}=L^2$; $V_0$ is invariant under integer shifts; $\{\varphi(t-k)\}$ is a Riesz (orthonormal) basis of $V_0$. Details: $V_{j-1}=V_j\oplus W_j$. Detail level $j$ corresponds to scale $2^j$.

### 7.2. Refinement equation and the wavelet {#wav-refinement}

**Formulas** (normalization $\sqrt2$; $h=f_0=$ `rec_lo`, $g=f_1=$ `rec_hi`):
$$\varphi(t)=\sqrt2\sum_kh[k]\varphi(2t-k),\qquad \psi(t)=\sqrt2\sum_kg[k]\varphi(2t-k).$$
In the frequency domain $\Phi(\omega)=m_0(\omega/2)\Phi(\omega/2)$, $m_0(\omega)=\tfrac1{\sqrt2}\sum_kh[k]e^{-ik\omega}$, $m_0(0)=1$, whence $\Phi(\omega)=\prod_{j\ge1}m_0(\omega/2^j)$. Orthonormality of the shifts $\iff\sum_k|\Phi(\omega+2\pi k)|^2=1$, whence $|m_0(\omega)|^2+|m_0(\omega+\pi)|^2=1$ — necessary but not sufficient (Lawton's/Cohen's criterion is also needed, §7.3).

Biorthogonal case: $\tilde\varphi$ is built from the reversed analysis lowpass (`dec_lo` reversed), $\langle\varphi(\cdot-k),\tilde\varphi(\cdot-m)\rangle=\delta_{km}$. The coefficients $c_j[k]=\langle f,\tilde\varphi_{j,k}\rangle$, $d_j[k]=\langle f,\tilde\psi_{j,k}\rangle$ are computed by Mallat's pyramid: $c_{j+1}=\downarrow2(h_0*c_j)$, $d_{j+1}=\downarrow2(h_1*c_j)$.

**The "wavelet crime" trap** (Strang–Nguyen): the samples $x[n]$ are not $c_0[n]$. For audio we take a purely discrete view (a filter bank on $\ell^2$) and use the MRA to visualize $\varphi,\psi$; coiflets give $x[n]\approx c_0[n]$ to high order.

### 7.3. Cascade algorithm and convergence {#wav-cascade}

**Algorithm.** $\varphi^{(i+1)}(t)=\sqrt2\sum_kh[k]\varphi^{(i)}(2t-k)$, $\varphi^{(0)}=\mathbf 1_{[0,1)}$. Discretely: $c^{(0)}=[1]$, $c^{(i+1)}=\sqrt2\,(\uparrow2\,c^{(i)})*h$ (with sum-to-one normalization the factor is $2$), $\varphi(n2^{-i})\approx c^{(i)}[n]$; equivalently $c^{(i)}=2^{i/2}\cdot$ the impulse response of $\prod_{k<i}H(z^{2^k})$. For $\psi$: one step with $g$, then the cascade with $h$. The support of $\varphi$ is $[0,L-1]$. One iteration costs $O(L2^i)$; 10–12 iterations are enough for display.

**Convergence.** In $L^2$: by Condition E (Strang–Nguyen) for the transition operator $T_{|m_0|^2}$: the eigenvalue $1$ is simple and all others have modulus $<1$; in practice $\iff s>0$ (§4.8). Uniform convergence requires a Hölder exponent $>0$.

**Divergence diagnostic** (for the visualization mode): $2^{-i}\sum_nc^{(i)}[n]^2$ is the squared $L^2$ norm of the iterate. Verified: for 5/3 it converges ($2.1386$), at $s=0$ it grows linearly, at $s=-0.56$ (`bior3.1`) it grows by a factor of $4^{0.56}\approx2.17$ per step.

### 7.4. Eigenvector method: φ at dyadic rational points {#wav-eigenvector}

$\varphi(n)=\sqrt2\sum_kh[k]\varphi(2n-k)$ for integer $n$: $v=Mv$, $M_{n,m}=\sqrt2\,h[2n-m]$, $n,m=0..L-1$; $v$ is the eigenvector for eigenvalue $1$, normalized so that $\sum_n\varphi(n)=1$ (partition of unity; at least one zero at $\pi$ is required). Then $\varphi(n+\tfrac12)=\sqrt2\sum_kh[k]\varphi(2n+1-k)$, and so on down to any dyadic level — exactly, with no cascade approximation error.

**Check.** The spectrum of $M$ contains $1,\tfrac12,\dots,2^{-(N-1)}$ when there are $N$ zeros at $\pi$ (verified: db2 — $1;\,0.683;\,0.5;\,0.183$; db4 contains $1;\,0.5;\,0.25;\,0.125$); $\sum_n\varphi(n+t)=1$ for dyadic $t$; agreement with the cascade as $i\to\infty$.

### 7.5. Vanishing moments ⇔ zeros at π {#wav-vanishing-moments}

$\tilde\psi$ (the analysis wavelet) has $\tilde N$ vanishing moments $\int t^k\tilde\psi=0$, $k<\tilde N$ $\iff$ $H_1$ has $\tilde N$ zeros at $z=1$ $\iff$ $F_0$ has $\tilde N$ zeros at $z=-1$ (since $H_1(z)=F_0(-z)$) $\iff$ $\varphi$ reproduces polynomials of degree $<\tilde N$ (Strang–Fix: $\Phi(2\pi k)$ has a zero of order $\tilde N$ for $k\ne0$). Discrete form for tests: the details of the samples of a polynomial of degree $<\tilde N$ are zero away from the edges. For `biorNr.Nd` $\tilde N=N_r$ (verified); for dbN it is $N$ on both sides.

### 7.6. Regularity {#wav-regularity}

Hölder: $|\varphi(t)-\varphi(\tau)|\le C|t-\tau|^\alpha$ (for $\alpha\le1$; beyond that, for derivatives); Sobolev: $\varphi\in H^s\iff\int(1+\omega^2)^s|\Phi|^2<\infty$; $\alpha\ge s-\tfrac12$.

Estimates: (a) the exact Sobolev exponent via the transition operator, $s=N-\log_4\rho(T_{|L|^2})$ (§4.8); (b) Daubechies' bound: if $m_0=\big(\tfrac{1+e^{-i\omega}}2\big)^NL(\omega)$ and $B=\sup|L|<2^{N-1}$, then $\varphi\in C^\alpha$ with $\alpha\ge N-1-\log_2B$; it is refined via $B_k=\sup|\prod_{j<k}L(2^j\omega)|^{1/k}$.

Values of $s$ (computed): Haar $0.5$; db2 $1.0$ (Hölder $\approx0.55$); db3 $1.415$; db4 $1.776$; 9/7 — $s(\varphi)=2.12$, $s(\tilde\varphi)=1.41$; 5/3 — $1.5/0.44$; `bior3.3` — $s(\tilde\varphi)=0.175$ (barely in $L^2$). Asymptotically the Hölder exponent of dbN is $\approx0.2075N$ ($1-\log_23/2$ per zero).

**Check.** The reference values above; consistency with the behavior of the cascade.

### 7.7. Wavelet families {#wav-families}

| family | defining property | length | vanishing moments | notes |
|---|---|---|---|---|
| Haar (db1) | orthogonal, symmetric | 2 | 1 | discontinuous |
| dbN | orthogonal, minimum phase, most moments for a given length | $2N$ | $N$ | support $[0,2N-1]$; pywt goes up to db38 |
| symN | same $\lvert H\rvert$ as dbN; phase closest to linear | $2N$ | $N$ | not strictly symmetric |
| coifN | moments for both $\psi$ and $\varphi$ | $6N$ | $\psi$: $2N$; $\varphi$: $2N-1$ | coif1: $L=6$ (verified) |
| biorNr.Nd (CDF, spline) | $F_0\propto(1+z^{-1})^{N_r}$, $\varphi$ is a B-spline of degree $N_r-1$; symmetric | varies | analysis: $N_r$; synthesis: $N_d$ | $p=(N_r+N_d)/2$ |
| CDF 9/7 (`bior4.4`) | 4 zeros at $-1$ on each side, roots of $B_4$ split | 9/7 | 4/4 | nearly orthogonal, $\kappa=1.32$; lossy JPEG 2000 |
| rbio | bior with the roles swapped | — | — | — |
| Battle–Lemarié | orthogonalized B-splines | infinite | spline order | IIR, exponential decay |
| Meyer / dmey | compact support in frequency | infinite / 62 | infinitely many | dmey is an FIR approximation (verified: 62) |
| Shannon | ideal brick wall | infinite | infinitely many | $\varphi=\mathrm{sinc}$, $1/t$ decay |

Formulas for the frequency-domain families:

- **Battle–Lemarié** of order $m$: $\Phi(\omega)=\hat B_m(\omega)\big/\sqrt{\sum_k|\hat B_m(\omega+2\pi k)|^2}$; $m_0$ is rational in $e^{i\omega}$ (IIR); implemented in the frequency domain (periodization, FFT) or as a truncated FIR [v2].
- **Meyer** (up to a phase factor $e^{\pm i\omega/2}$): $\Phi(\omega)=1$ for $|\omega|\le2\pi/3$, $\cos\!\big[\tfrac\pi2\nu\big(\tfrac{3|\omega|}{2\pi}-1\big)\big]$ for $2\pi/3\le|\omega|\le4\pi/3$, $0$ beyond;
$|\Psi(\omega)|=\sin\!\big[\tfrac\pi2\nu\big(\tfrac{3|\omega|}{2\pi}-1\big)\big]$ for $2\pi/3\le|\omega|\le4\pi/3$, $\cos\!\big[\tfrac\pi2\nu\big(\tfrac{3|\omega|}{4\pi}-1\big)\big]$ for $4\pi/3\le|\omega|\le8\pi/3$, $0$ otherwise; $\nu(x)=x^4(35-84x+70x^2-20x^3)$ on $[0,1]$, $\nu(x)+\nu(1-x)=1$. Smoothness in frequency gives fast decay in time [v2].
- **Shannon:** $\Phi=\mathbf 1_{[-\pi,\pi]}$, $|\Psi|=\mathbf 1_{\pi\le|\omega|\le2\pi}$ — the limit of dbN as $N\to\infty$.

**Check.** The coefficients of all FIR families are compared against pywt; orthogonality and PR; the number of zeros at $-1$; lengths.

---

## 8. Frequency responses of filters and banks [core] {#freq}

### 8.1. Terms {#freq-terms}

Standard terms:

- **Magnitude response** — $|H(e^{i\omega})|$.
- **Log-magnitude response** — $20\lg|H|$ in dB, usually on a logarithmic frequency axis.
- **Phase response** — $\varphi(\omega)=\arg H$.
- **Nyquist plot** — the complex $H(e^{i\omega})$ as a locus in the complex plane.
- **Group delay** — $\tau_g(\omega)$; **phase delay** $\tau_p(\omega)$.
- **Impulse response** — $h[n]$; **step response** — $s[n]$.

"Amplitude-frequency spectrum", "phase-frequency spectrum" and transposed abbreviations of these terms are not standard terms (they are apparently slips for magnitude response / phase response). Distinguish the **signal spectrum** $|X(e^{i\omega})|$ from the **filter response** $|H(e^{i\omega})|$.

### 8.2. Magnitude and log-magnitude response {#freq-magnitude}

**Computation.** On a uniform grid: an FFT with zero padding to $K\ge8L$ (typically 4096); at arbitrary frequencies (a logarithmic axis for audio): Horner or Goertzel at each point. Conversion to dB with a floor: $20\lg\max(|H|,10^{-300/20})$; exact zeros (at $\pi$ for wavelet lowpass filters) are shown as $-\infty$ with a label. Normalization: a wavelet lowpass has $|H_0(0)|=\sqrt2$ ($+3.01$ dB) — a toggle "absolute / relative to $H(0)$". Axis: $\omega\in[0,\pi]$ or $f=\omega f_s/2\pi$; the nominal band of detail level $j$ is $[f_s/2^{j+1},f_s/2^j]$.

**Check.** FFT and Horner agree; $|H(0)|=\lvert\sum h\rvert$, $|H(\pi)|=\lvert\sum(-1)^nh[n]\rvert$.

### 8.3. Phase response: wrapped and unwrapped {#freq-phase}

The wrapped phase lies in $(-\pi,\pi]$; the unwrapped phase adds $2\pi$ at jumps larger than $\pi$. Traps: (1) at zeros of $H$ on the unit circle the phase jumps by $\pi$ (a sign change of the real amplitude $A(\omega)$) — this is not wrapping, and unwrapping must not "fix" it; for linear-phase filters show the signed $A(\omega)$ and the linear term separately; (2) a coarse grid produces spurious jumps; (3) where $|H|$ is small (the stopband), the phase is numerical noise: mask it, e.g. below $-120$ dB relative to the maximum. Linear phase: $\varphi=-\omega M/2$ ($+\pi/2$ for antisymmetric filters) plus jumps of $\pi$.

### 8.4. Group delay and phase delay {#freq-group-delay}

**Stable formula** (without differentiating the unwrapped phase):
$$\tau_g(\omega)=\operatorname{Re}\frac{H_n(e^{i\omega})}{H(e^{i\omega})},\qquad H_n=\text{DTFT}\{n\,h[n]\},$$
since $\frac d{d\omega}\ln H=-iH_n/H$ and $\varphi'=-\operatorname{Re}(H_n/H)$. Two FFTs, no unwrapping; undefined at zeros of $H$ (peaks near zeros close to the circle are a real effect): mask by $|H|$. For an IIR $B/A$: $\tau=\tau_B-\tau_A$. Units: samples; divide by $f_s$ to get seconds. The phase delay $\tau_p=-\varphi_{\text{unwr}}(\omega)/\omega$ requires the unwrapped phase on the correct branch as $\omega\to0$ ($\varphi(0)\in\{0,\pi\}$).

**Check.** A symmetric filter gives $\tau_g\equiv M/2$; a pure delay $z^{-k}$ gives $k$; comparison with the numerical derivative of the unwrapped phase on a smooth stretch.

### 8.5. Impulse and step response {#freq-impulse-step}

The impulse response $h[n]$ is drawn as stems, taking the Laurent offset into account; the step response is $s[n]=\sum_{k\le n}h[k]$, $s[\infty]=H(1)$. Overshoot and ringing (Gibbs) are visible: the step response of Haar is monotone, that of db4 rings. For audio: a symmetric impulse response gives pre-ringing, a minimum-phase one gives post-ringing; this is transient smearing.

### 8.6. Pole–zero map {#freq-zero-pole}

FIR $H(z)=z^{-(L-1)}\cdot$(polynomial): $L-1$ zeros and a pole of multiplicity $L-1$ at $z=0$ (causal indexing; with negative indices, poles at $\infty$). IIR: stability means poles inside the unit circle. For wavelet filters: a cluster of $N$ zeros at $-1$ (the computed roots spread out over a circle of radius $\sim u^{1/N}$ — $10^{-4}$ for $N=4$ — so show the **known** multiplicity, not the roots found), reciprocal pairs for linear phase, all zeros inside for minimum phase (`rec_lo` of dbN). Look for the remaining zeros in the quotient left after exact division by $(1+z^{-1})^N$.

### 8.7. Band metrics and half-band symmetry {#freq-band-metrics}

- The $-3$ dB cutoff: $|H(\omega_c)|=|H(0)|/\sqrt2$. **For any orthogonal lowpass it lies exactly at $\pi/2$**: by power symmetry $|H_0(\pi/2)|^2=1=|H_0(0)|^2/2$ — a good test.
- Passband ripple $\delta_p=\max\big||H|/|H(0)|-1\big|$ (in dB, $20\lg\frac{1+\delta_p}{1-\delta_p}$); stopband attenuation $A_s=-20\lg\max_{\text{stop}}|H|/|H(0)|$; transition band $\Delta\omega=\omega_s-\omega_p$.
- Maximally flat filters have no ripple but a wide transition band: report $\omega$ at the $-1,-3,-20,-40$ dB levels.
- Power symmetry (orthogonal bank, $\sqrt2$): $|H_0(\omega)|^2+|H_0(\pi-\omega)|^2=2$ (for real filters $|H_0(\pi-\omega)|=|H_0(\omega+\pi)|$); $|H_0|^2$ is half-band.
- Power complementarity: $|H_0(\omega)|^2+|H_1(\omega)|^2=2$. Biorthogonal banks do not satisfy it; show the "orthogonality defect" $|H_0|^2+|H_1|^2-2$.

### 8.8. Equivalent subband filters {#freq-equivalent-filters}

By the Noble identities, the details of level $j$ are obtained with a single filter followed by $\downarrow2^j$:
$$H^{eq}_j(z)=H_1\big(z^{2^{j-1}}\big)\prod_{k=0}^{j-2}H_0\big(z^{2^k}\big),\qquad G^{eq}_J(z)=\prod_{k=0}^{J-1}H_0\big(z^{2^k}\big),$$
and synthesis is the same with $F_0,F_1$. Length for $L_0=L_1=L$: $(2^j-1)(L-1)+1$. The magnitude response of the set $\{H^{eq}_1,\dots,H^{eq}_J,G^{eq}_J\}$ shows the octave split (nominally $[\pi/2^j,\pi/2^{j-1}]$), the overlap between neighbors (the potential for aliasing) and the side lobes caused by the leakage of $H_0$ ($-13$ dB for Haar). Computation: an exact product of polynomials, or a product of DTFTs on a grid, $H^{eq}_j(e^{i\omega})=H_1(e^{i2^{j-1}\omega})\prod H_0(e^{i2^k\omega})$. With $\sqrt2$ normalization, orthogonal banks have $\|h^{eq}_j\|=1$.

Wavelet packets: node $(j,n)$ is the product of $H_0/H_1$ along the bits of the path; **the frequency order is the Gray code**, not the natural (Paley) order, because $\downarrow2$ after a highpass mirrors the spectrum.

**Check.** Energy $\|h^{eq}_j\|^2=1$ (orthogonal banks); a direct multilevel DWT of a unit impulse gives the same impulse responses.

### 8.9. Spectra of φ and ψ {#freq-phi-psi-spectra}

$\Phi(\omega)=\prod_{j=1}^{\infty}m_0(\omega/2^j)$, $m_0=H(e^{i\omega})/H(1)$ ($H=f_0$ for $\varphi$; the reversed $h_0$ for $\tilde\varphi$ — reversal does not affect the modulus); truncate after $J$ factors once $2^{-J}|\omega|$ is small ($J=20..30$); $\Psi(\omega)=m_1(\omega/2)\Phi(\omega/2)$, $m_1=F_1/\sqrt2$ (with $m_0=F_0/\sqrt2$).

What you see: $|\Phi|$ is a lowpass with zeros of order $N$ at $2\pi k$, $k\ne0$ (Strang–Fix), which show up as deep notches on the log-magnitude plot; $|\Psi|$ is bandpass, peaking in the octave $[\pi,2\pi]$. The flatness of $|m_0|$ at $\pi$ (a zero of order $N$) $\leftrightarrow|\Psi(\omega)|\sim|\omega|^{\tilde N}$ near zero: on a log–log plot the slope is $20\tilde N$ dB/decade — a visual "vanishing-moment counter" ($\tilde N$ is the number of zeros at $\pi$ of the dual lowpass).

**Check.** $\sum_k|\Phi(\omega+2\pi k)|^2=1$ (orthogonal, truncated sum); $\sum_{j\in\mathbb Z}|\Psi(2^j\omega)|^2=1$ for $\omega\neq0$ (follows from $|m_0|^2+|m_1|^2=1$ by telescoping).

### 8.10. Time–frequency tiling: Heisenberg boxes {#freq-heisenberg}

For an atom $g$, $\|g\|=1$: $\bar t=\int t|g|^2$, $\sigma_t^2=\int(t-\bar t)^2|g|^2$, $\sigma_\omega^2=\frac1{2\pi}\int(\omega-\bar\omega)^2|\hat g|^2$ (for real atoms, over the positive frequencies, otherwise $\bar\omega=0$); $\sigma_t\sigma_\omega\ge\tfrac12$, with equality for a Gaussian. The discrete version used by the app works with the equivalent filters: $\bar n,\sigma_n$ from $|h|^2$, $\bar\omega,\sigma_\omega$ from $|H|^2$ on $[0,\pi]$. There is no exact bound $\tfrac12$ for finite sequences — the quantities are descriptive.

Tilings: STFT — identical boxes at all frequencies; DWT — $\sigma_t\propto2^j$, $\sigma_\omega\propto2^{-j}$, one coefficient per $2^j$ samples; packets — any admissible dyadic tiling (chosen by the best basis). The continuous Haar $\psi$ has $\sigma_\omega=\infty$ ($|\Psi|\sim1/\omega$) — a vivid illustration of poor frequency localization; hence the discrete $\sigma$ on $[0,\pi]$. A box is drawn centered at $(k2^j,\bar\omega_j)$ with size $2\sigma_t\times2\sigma_\omega$.

### 8.11. Response of the whole analysis → synthesis chain {#freq-full-chain}

**One level.** The plots that prove PR: $|T(e^{i\omega})|\equiv1$ (0 dB), $\arg T=-\ell\omega$, the group delay of $T$ is constant and equal to $\ell$, $|A|\equiv0$ (numerically below $-250$ dB in double).

**$J$ levels.** $\hat X(z)=\sum_{k=0}^{2^J-1}A_k(z)X(zW^k)$, $W=e^{-2\pi i/2^J}$,
$$A_k(z)=\sum_{i:\ (2^J/M_i)\mid k}\frac1{M_i}F^{eq}_i(z)H^{eq}_i(zW^k),$$
where $M_i$ is the downsampling factor of subband $i$ ($2^j$ for the details of level $j$, $2^J$ for the approximation). PR: $A_0=z^{-\ell_J}$, $A_k=0$ for $k\ne0$. Show $|A_0|$ and $\max_k|A_k|$ (or each $A_k$). With gains $g_i$ (zeroing, equalizer): $A^{(g)}_k=\sum_i\frac{g_i}{M_i}F^{eq}_iH^{eq}_i(zW^k)$ — the plot shows the aliasing created by the editing (aliasing energy — §14.6).

**Time variance.** With modified $g_i$ the system is periodically time-varying with period $2^J$: show $2^J$ impulse responses (an impulse at phases $0..2^J-1$) — the differences between them are the aliasing.

**Check.** The theoretical $A_k$ match the measured ones: the chain's responses to $\delta[n-m]$, $m=0..2^J-1$, are related to $A_k$ by a DFT over the phase.

---

## 9. Classical frequency-selective filters [v1] {#classic}

### 9.1. Filter types and specification {#cls-types}

Lowpass (LP), highpass (HP), bandpass (BP), band-stop (BS, band-reject), narrow band-stop (notch), all-pass (phase) filters. FIR and IIR. Specification: band edges $\omega_p,\omega_s$, tolerances $\delta_p,\delta_s$ (or ripple $A_p$ dB and attenuation $A_s$ dB).

### 9.2. Linear-phase FIR: what each type can implement {#cls-fir-types}

| type | lowpass | highpass | bandpass | band-stop | differentiator / Hilbert |
|---|---|---|---|---|---|
| I | yes | yes | yes | yes | no / no |
| II | yes | no ($H(\pi)=0$) | yes | no | no / no |
| III | no | no ($H(0)=H(\pi)=0$) | yes | no | yes / yes |
| IV | no ($H(0)=0$) | yes | yes | no | yes / yes |

### 9.3. Window method and the Kaiser window {#cls-window-method}

**Formulas.** The ideal lowpass shifted to $M/2$: $h_d[n]=\dfrac{\sin\omega_c(n-M/2)}{\pi(n-M/2)}$ (at $n=M/2$ it equals $\omega_c/\pi$), $h=h_dw$, $\omega_c=(\omega_p+\omega_s)/2$. Windows ($n=0..M$): rectangular; Hann $0.5-0.5\cos\frac{2\pi n}M$; Hamming $0.54-0.46\cos\frac{2\pi n}M$; Blackman $0.42-0.5\cos\frac{2\pi n}M+0.08\cos\frac{4\pi n}M$.

| window | side lobe | main-lobe width | filter error | equiv. Kaiser $\beta$ | equiv. Kaiser transition |
|---|---|---|---|---|---|
| rectangular | −13 dB | $4\pi/(M+1)$ | −21 dB | 0 | $1.81\pi/M$ |
| Hann | −31 dB | $8\pi/M$ | −44 dB | 3.86 | $5.01\pi/M$ |
| Hamming | −41 dB | $8\pi/M$ | −53 dB | 4.86 | $6.27\pi/M$ |
| Blackman | −57 dB | $12\pi/M$ | −74 dB | 7.04 | $9.19\pi/M$ |

(Oppenheim–Schafer.) **Kaiser:** $w[n]=I_0\big(\beta\sqrt{1-((n-M/2)/(M/2))^2}\big)/I_0(\beta)$; with $A=-20\lg\delta$, $\delta=\min(\delta_p,\delta_s)$:
$$\beta=\begin{cases}0.1102(A-8.7),&A>50\\0.5842(A-21)^{0.4}+0.07886(A-21),&21\le A\le50\\0,&A<21\end{cases}\qquad M=\frac{A-8}{2.285\,\Delta\omega}.$$
The window method gives $\delta_p\approx\delta_s$. $I_0$ is a power series (about 30 terms for $\beta\le20$).

**Check.** The measured attenuation and transition width agree with the formulas; the coefficients are symmetric.

### 9.4. Frequency sampling {#cls-frequency-sampling}

Set $A(\omega_k)$ at $\omega_k=2\pi k/L$ (with linear phase) and take the inverse DFT; 1–3 transition samples are optimized to reduce the ripple. Exact match at the nodes, ripple between them. Cheap and inferior to the optimal methods; convenient for arbitrary curves (an equalizer).

### 9.5. Least squares {#cls-least-squares}

Minimize $\int_{\text{bands}}W(\omega)(A(\omega)-D(\omega))^2d\omega$, $A(\omega)=\sum_{k=0}^Ka_k\cos k\omega$ (type I, $M=2K$). Normal equations $Qa=b$:
$$Q_{kl}=\int W\cos k\omega\cos l\omega\,d\omega=\tfrac12\int W\big[\cos(k-l)\omega+\cos(k+l)\omega\big]d\omega,\quad b_k=\int WD\cos k\omega\,d\omega$$
— "Toeplitz + Hankel" (§2.4); the integrals have closed forms for piecewise-constant $W,D$; solve with Cholesky ($Q$ is SPD). This minimizes the error energy: the total error is smaller than with equiripple, but there are spikes at the band edges.

### 9.6. Parks–McClellan (Remez, equiripple) {#cls-parks-mcclellan}

Minimize $\max_\omega W(\omega)|A(\omega)-D(\omega)|$. Alternation theorem: $A$ (type I, $K+1$ coefficients) is optimal $\iff$ the error has at least $K+2$ alternating extrema. Remez algorithm: initial $K+2$ frequencies → solve for $a$ and $\delta$ (barycentric Lagrange interpolation) → new extrema on a dense grid ($\ge16(K+1)$ points) → repeat until stable. Length estimate (Kaiser): $M\approx\dfrac{-10\lg(\delta_p\delta_s)-13}{2.324\,\Delta\omega}$. Weights: $W=1$ in the passband, $\delta_p/\delta_s$ in the stopband. Traps: non-convergence for very long filters and wide "don't care" regions, non-monotone transition bands in multiband problems — check the number of alternations.

For banks: with $\omega_p+\omega_s=\pi$ and $\delta_p=\delta_s$ the optimal filter turns out half-band (the Vaidyanathan–Nguyen trick); raising the center tap by $\delta_s$ makes $P\ge0$ on the circle, after which spectral factorization yields an orthogonal bank with sharp cutoffs but only one zero at $\pi$ (the Smith–Barnwell procedure) [v2].

### 9.7. Highpass, bandpass and band-stop from a lowpass prototype {#cls-transformations}

- **Spectral inversion** (type I only, $M$ even): $h_{\text{HP}}[n]=\delta[n-M/2]-h_{\text{LP}}[n]$, $A_{\text{HP}}=1-A_{\text{LP}}$.
- **Modulation:** $h_{\text{HP}}[n]=(-1)^nh_{\text{LP}}[n]$, $H_{\text{HP}}(\omega)=H_{\text{LP}}(\omega-\pi)$ — a mirror image about $\pi/2$, $\omega_c\to\pi-\omega_c$; works for all types (II → IV). This is exactly the relation $H_1(z)=\pm F_0(-z)$ in banks.
- **Bandpass:** $h_{\text{BP}}[n]=2\cos(\omega_0(n-M/2))\,h_{\text{LP}}[n]$ — a shift to $\pm\omega_0$, bandwidth $2\omega_c$; or the difference of two lowpass filters of the same length with the same window.
- **Band-stop:** $h_{\text{BS}}[n]=\delta[n-M/2]-h_{\text{BP}}[n]$ (type I).

### 9.8. All-pass and notch filters {#cls-allpass-notch}

First-order all-pass: $A(z)=\dfrac{-a+z^{-1}}{1-az^{-1}}$, $|a|<1$, $|A(e^{i\omega})|=1$; complex pairs as second-order sections. Applications: phase equalization, fractional delay, Hilbert pairs, IIR half-band filters built from all-passes: $H_0=\tfrac12[A_0(z^2)+z^{-1}A_1(z^2)]$ — a power-complementary IIR bank [v2].

Notch: $H(z)=\dfrac{1-2\cos\omega_0z^{-1}+z^{-2}}{1-2r\cos\omega_0z^{-1}+r^2z^{-2}}$, $-3$ dB width $\approx2(1-r)$ rad; equivalent to $\tfrac12(1+A_2(z))$ with a second-order all-pass. Example: 50 Hz mains hum. An FIR notch (zeros on the circle only) comes out wide.

### 9.9. Analog IIR prototypes {#cls-iir-prototypes}

- **Butterworth:** $|H(j\Omega)|^2=1/(1+(\Omega/\Omega_c)^{2N})$; maximally flat at $0$; poles evenly spaced on a circle of radius $\Omega_c$ in the left half-plane; $N\ge\dfrac{\lg\big[(10^{A_s/10}-1)/(10^{A_p/10}-1)\big]}{2\lg(\Omega_s/\Omega_p)}$.
- **Chebyshev I:** $1/(1+\varepsilon^2T_N^2(\Omega/\Omega_p))$ — equiripple passband (ripple $10\lg(1+\varepsilon^2)$ dB), monotone stopband, poles on an ellipse; $N\ge\dfrac{\operatorname{arccosh}\sqrt{(10^{A_s/10}-1)/(10^{A_p/10}-1)}}{\operatorname{arccosh}(\Omega_s/\Omega_p)}$.
- **Chebyshev II:** $1/\big(1+1/(\varepsilon^2T_N^2(\Omega_s/\Omega))\big)$ — flat passband, equiripple stopband, zeros on the imaginary axis.
- **Elliptic (Cauer):** equiripple in both bands, the minimum order for a given specification; Jacobi elliptic functions.
- **Bessel:** maximally flat group delay at $0$ (reverse Bessel polynomials), good step response; the flat group delay is destroyed by the warping of the bilinear transform (use it with oversampling, or design a digital flat delay directly).

### 9.10. Analog → digital: bilinear transform and impulse invariance {#cls-bilinear}

**Bilinear:** $s=\frac2T\frac{1-z^{-1}}{1+z^{-1}}$; the imaginary axis maps onto the circle with the warping $\Omega=\frac2T\tan\frac\omega2$; the left half-plane maps inside the circle (stability is preserved); there is no aliasing. **Prewarping:** $\Omega_c=\frac2T\tan\frac{\omega_c}2$ for all critical frequencies before the prototype is designed ($T$ cancels). **Impulse invariance:** $h[n]=Th_a(nT)$, $s_k\to e^{s_kT}$, $H(e^{i\omega})=\sum_kH_a\big(j(\omega-2\pi k)/T\big)$ — with aliasing; suitable only for well band-limited lowpass/bandpass filters, unusable for highpass/band-stop. Lowpass → highpass/bandpass/band-stop transformations are done in the analog domain ($s\to\Omega_c/s$, $s\to(s^2+\Omega_0^2)/(Bs)$) or by digital all-pass substitutions (Constantinides).

### 9.11. Implementation: biquad sections, stability {#cls-biquads}

$H(z)=g\prod_k\dfrac{b_{0k}+b_{1k}z^{-1}+b_{2k}z^{-2}}{1+a_{1k}z^{-1}+a_{2k}z^{-2}}$: each pole pair is matched with the nearest zero pair, the sections are ordered by pole radius, the gain is distributed among them; transposed direct form II. Do not implement a high order as a single polynomial: the pole sensitivity $\partial p_i/\partial a_k\sim1/\prod_{j\ne i}(p_i-p_j)$ blows up when poles cluster (a narrowband lowpass has its poles near $z=1$). Stability: all $|p_k|<1$; for a section $1+a_1z^{-1}+a_2z^{-2}$ it is the triangle $|a_2|<1$, $|a_1|<1+a_2$. Check after coefficient quantization (float32).

### 9.12. Zero-phase forward–backward filtering {#cls-filtfilt}

$y=R\big(h*R(h*x)\big)$, where $R$ is time reversal: the response is $|H(e^{i\omega})|^2$, the phase is zero, the attenuation in dB doubles; non-causal (offline). Edge transients: signal extension (odd reflection of about $3\times$ the filter order) and steady-state initial conditions (as in `scipy.signal.filtfilt`). For a linear-phase FIR, zero phase is just a shift by $M/2$.

### 9.13. Relation to wavelet banks {#cls-wavelet-relation}

1. A two-channel bank is a complementary lowpass/highpass pair around $\pi/2$; orthogonal: $|H_0|^2+|H_1|^2=2$, $-3$ dB exactly at $\pi/2$; the QMF mirroring $|H_1(\omega)|=|H_0(\pi-\omega)|$ is the modulation of §9.7.
2. The Daubechies maximally flat filter is the FIR analog of Butterworth (flat at $0$ and $\pi$, no ripple, a wide transition); sharper banks come from equiripple half-band filters + factorization, at the cost of vanishing moments.
3. Why FIR: exact PR with an FIR inverse ($\det$ is a monomial), finite support (compactly supported wavelets), linear phase is possible (biorthogonal), unconditional stability, exact finite matrices. IIR orthogonal banks exist (built from all-pass sections, Butterworth-based), but the synthesis is non-causal or infinite, and the edges are harder [v2 / research].
4. A wavelet subband is a poor bandpass filter (the transitions overlap, §8.8). A frequency-selective equalizer needs classical FIR/IIR filters or the STFT; the app shows both approaches side by side.

---

## 10. Lifting [v1] {#lifting}

### 10.1. Laurent polynomials and the Euclidean algorithm {#lift-euclid}

The degree $|p|$ = (highest exponent − lowest exponent). Division $p=qd+r$ with $|r|<|d|$ is **not unique** (terms can be cancelled from either end). GCD by Euclid: $a_0=a$, $b_0=b$; $a_{i+1}=b_i$, $b_{i+1}=a_i\bmod b_i$ until $b_n=0$; $a_n$ is the GCD up to a monomial $cz^k$.

### 10.2. Daubechies–Sweldens factorization {#lift-factorisation}

**Theorem.** A polyphase matrix of FIR filters with $\det=1$ factors as
$$H_p(z)=\begin{pmatrix}K&0\\0&1/K\end{pmatrix}\prod_{i=1}^{m}\begin{pmatrix}1&U_i(z)\\0&1\end{pmatrix}\begin{pmatrix}1&0\\-P_i(z)&1\end{pmatrix}.$$
Proof: Euclid on the polyphase components of the lowpass (they are coprime, since $\det=1$) plus one final step for the highpass. If $\det H_p=cz^{-k}$, normalize first: multiply the highpass by $z^k/c$. The factorization is not unique — choose one with short steps whose coefficients are small in magnitude.

**Analysis algorithm** (our notation of §0.5, rows are $s,d$): lazy split into $x^{(0)},x^{(1)}$; predict $d=x^{(1)}-P(z)x^{(0)}$; update $s=x^{(0)}+U(z)d$; scale $s\leftarrow Ks$, $d\leftarrow d/K$. Synthesis runs the steps in reverse order with opposite signs; invertibility does not depend on what exactly $P$ and $U$ compute (rounding included).

**Check.** Multiplying the factors back together gives $H_p$; $\det=1$.

### 10.3. LeGall 5/3 and the integer version {#lift-53}

$$d[n]=x[2n+1]-\tfrac12\big(x[2n]+x[2n+2]\big),\qquad s[n]=x[2n]+\tfrac14\big(d[n-1]+d[n]\big).$$
The equivalent lowpass is $(-1,2,6,2,-1)/8$ (centered at $2n$), the highpass $(-\tfrac12,1,-\tfrac12)$ (centered at $2n+1$) — sum-to-one normalization, $K=1$; $P(z)=\tfrac12(1+z)$, $U(z)=\tfrac14(1+z^{-1})$. With $K=\sqrt2$ it is `bior2.2` up to sign and shift.

**Integer version (JPEG 2000, lossless):** $d[n]=x[2n+1]-\lfloor(x[2n]+x[2n+2])/2\rfloor$, $s[n]=x[2n]+\lfloor(d[n-1]+d[n]+2)/4\rfloor$; inverse: $x[2n]=s[n]-\lfloor(d[n-1]+d[n]+2)/4\rfloor$, $x[2n+1]=d[n]+\lfloor(x[2n]+x[2n+2])/2\rfloor$. Edges: WS mirroring of the indices. Bit-depth headroom: the details need about one extra bit per level.

**Check.** An exact round trip for random int16/int32, including extreme values (an overflow test); the real-valued version equals the convolutional `bior2.2` up to a normalization correction.

### 10.4. CDF 9/7 {#lift-97}

$$\begin{aligned}d&\leftarrow d+\alpha(s_n+s_{n+1}),& s&\leftarrow s+\beta(d_{n-1}+d_n),\\ d&\leftarrow d+\gamma(s_n+s_{n+1}),& s&\leftarrow s+\delta(d_{n-1}+d_n),\\ s&\leftarrow\zeta s,& d&\leftarrow d/\zeta,\end{aligned}$$
$\alpha=-1.586134342$, $\beta=-0.05298011854$, $\gamma=0.8829110762$, $\delta=0.4435068522$, $\zeta=1.149604398$ (Daubechies–Sweldens 1998). Verified: the equivalent lowpass of these steps matches `bior4.4.dec_lo` to 9 digits (sum $\sqrt2$), and the highpass $=-$`dec_hi`. JPEG 2000 scales the bands differently, so its $K$ differs by a normalization. The published 10-digit constants give a deviation of $\sim3\cdot10^{-10}$ (verified) — for full double precision they have to be computed from the filter with the Euclidean algorithm.

### 10.5. Benefits and traps {#lift-benefits}

Benefits: in-place operation (even and odd samples in one array); asymptotically about half as many multiplications for long filters (Daubechies–Sweldens); integer-to-integer invertible transforms (Calderbank–Daubechies–Sweldens–Yeo 1998) — rounding inside the steps does not break invertibility; symmetric edges are trivial (§6.6); SIMD-friendly. Traps: the steps depend on each other sequentially (less parallelism per step); no gain for Haar; rounding differs from the convolutional version (both $\sim u$); large step coefficients ($|\alpha|\approx1.59$) slightly amplify errors — different factorizations of the same bank are not numerically equivalent; integer lifting is nonlinear, so frequency analysis of it is only approximate.

**Check.** Lifting = convolution on random signals (periodic and symmetric edges) to $10^{-14}$.

### 10.6. Second-generation wavelets {#lift-second-generation}

Lifting without translation and dilation invariance (Sweldens): non-uniform samples, intervals, weighted measures, graphs; the prediction is an interpolating polynomial over the neighbors, the update preserves moments. Building new wavelets from the lazy one: predict with $N$-point Deslauriers–Dubuc interpolation (the 1/7 factorization of §4.8 is exactly a 4-point prediction without an update), then update for $\tilde N$ dual moments [research].

---

## 11. Transform variants {#transforms}

### 11.1. DWT, Mallat's pyramid [core] {#dwt-mallat}

**Algorithm.** $a_0=x$; $a_j=\downarrow2(h_0*a_{j-1})$, $d_j=\downarrow2(h_1*a_{j-1})$, $j=1..J$; synthesis in reverse order. Cost $\sum_j(N/2^j)(L_0+L_1)\approx N(L_0+L_1)$ multiplications, memory $O(N)$; maximum number of levels $\lfloor\log_2(N/(L-1))\rfloor$; layout $[a_J,d_J,\dots,d_1]$. **No shift invariance:** shifting the input by 1 sample strongly redistributes the energy between levels — show this in the app.

**Check.** PR in all modes; agreement with `pywt.wavedec/waverec`; Parseval for orthogonal banks with periodization.

### 11.2. SWT / à trous / MODWT [v1] {#swt-modwt}

**SWT.** No downsampling; the level-$j$ filters are dilated with holes: $h^{(j)}(z)=H(z^{2^{j-1}})$; $a_j=h_0^{(j)}*a_{j-1}$, $d_j=h_1^{(j)}*a_{j-1}$; redundancy $J+1$; cost $O(NLJ)$; equivariance under (circular) shifts. Inverse: $a_{j-1}=\tfrac12\big(f_0^{(j)}*a_j+f_1^{(j)}*d_j\big)$ with a delay alignment of $\ell\cdot2^{j-1}$ at each level. pywt `swt` uses periodic extension, $2^J\mid N$. SWT = the DWT of all $2^J$ circular shifts, organized without repetition.

**MODWT** (Percival–Walden 2000): filters $\tilde h=h/\sqrt2$, then $|\tilde H_0|^2+|\tilde H_1|^2=1$ and $\|x\|^2=\sum_j\|\tilde W_j\|^2+\|\tilde V_J\|^2$ exactly; any $N$. For display the coefficients are shifted by the group delay of the equivalent filter so that they line up with events in time. **MRA decomposition** $x=\sum_jD_j+S_J$, where $D_j$ is the synthesis of one level: it is additive, and each component is an LTI filtering; ideal for display and for zeroing a subband without aliasing.

**Check.** The MODWT energy identity; $\sum D_j+S_J=x$; shift equivariance.

### 11.3. Wavelet packets and the best basis [v1] {#wavelet-packets}

Full tree of depth $J$: $2^J$ leaves of $N/2^J$ each; cost $O(NLJ)$. Order: natural (Paley) or frequency (Gray code — `order='freq'` in pywt), see §8.8. **Best basis** (Coifman–Wickerhauser 1992): an additive cost $\mathcal M(c)=\sum_k\mu(|c_k|)$, $\mu(0)=0$: entropy $-\sum c_k^2\log c_k^2$ (with $0\log0=0$, normalized by $\|x\|^2$), log-energy $\sum\log c_k^2$ (with a guard against zeros), $\ell^p$ ($\sum|c_k|^p$, $p<2$), threshold count $\#\{|c_k|>\varepsilon\}$. Bottom-up: a node is replaced by its children if $\mathcal M(\text{children})<\mathcal M(\text{node})$; $O(NJ)$ once the tree is built; optimal among all admissible bases (whose number grows doubly exponentially). Exact for orthogonal banks (costs are comparable across nodes), approximate for biorthogonal ones.

**Check.** PR from any admissible basis; the basis found costs no more than the full decomposition and the root; agreement with exhaustive search for $J\le4$.

### 11.4. M-channel banks [v2] {#m-band}

$M$ channels, $\downarrow M$; PR $\iff F_pE=z^{-k}I$ ($\det$ is a monomial for FIR). Cosine-modulated banks (pseudo-QMF, MDCT/TDAC as a lapped transform): $h_k[n]=2p[n]\cos\!\big(\tfrac\pi M(k+\tfrac12)(n-\tfrac{L-1}2)+(-1)^k\tfrac\pi4\big)$ — the MP3/AAC context. M-band wavelets: one $\varphi$, $M-1$ wavelets, dilation by $M$. Implemented on the common polyphase core.

### 11.5. Dual-tree complex DWT (Kingsbury) [v2] {#dtcwt}

Two real DWT trees $a,b$. Level 1: two different sets ("near-symmetric" biorthogonal) or one set shifted by one sample. Levels $\ge2$: q-shift filters, $h_b[n]=h_a[L-1-n]$, where $h_a$ has a group delay of $\approx\tfrac14$ sample, so the trees drift apart by $\tfrac12$ sample at each level, $\psi_b\approx\mathcal H\{\psi_a\}$ (the Hilbert transform), and $\psi_a+i\psi_b$ is nearly analytic. Redundancy 2 (1D), approximate shift invariance, aliasing between adjacent levels strongly suppressed. For audio: smooth subband envelopes, onset detection, subband editing with less aliasing. Requires the Kingsbury tables (near_sym_a/b, qshift_a…d, as in the Python package `dtcwt`).

**Check.** PR of each tree; the group-delay difference of the equivalent filters $\approx\tfrac12$ sample of the input level; an invariance metric: the variation of the level-$j$ energy over shifts $0..2^j-1$ — a few percent, versus large values for the DWT.

### 11.6. Continuous wavelet transform (CWT) [v1] {#cwt}

**Definition.** $W(a,b)=\frac1{\sqrt a}\int x(t)\psi^*\big(\tfrac{t-b}a\big)dt$ ($L^2$ normalization: equal energy across scales) or $\frac1a\int\cdots$ ($L^1$: a sinusoid of amplitude $A$ gives a scale-independent $|W|$ — more convenient for audio). Admissibility: $C_\psi=\int_0^\infty|\Psi(\omega)|^2\frac{d\omega}\omega<\infty$ ($\Rightarrow\Psi(0)=0$); inversion: $x=\frac1{C_\psi}\iint W\psi_{a,b}\frac{da\,db}{a^2}$; Morlet's single-integral formula: $x(t)=\operatorname{Re}\frac1{C'_\psi}\int W(a,t)a^{-3/2}da$, $C'_\psi=\int_0^\infty\Psi^*(\omega)\frac{d\omega}\omega$.

**Wavelets** (Torrence–Compo 1998, Table 1; $\mathcal H$ is the Heaviside function):

| wavelet | $\psi(t)$ | $\Psi(s\omega)$ | Fourier wavelength $\lambda$ | e-folding time |
|---|---|---|---|---|
| Morlet, $\omega_0$ | $\pi^{-1/4}e^{i\omega_0t}e^{-t^2/2}$ | $\pi^{-1/4}\mathcal H(\omega)e^{-(s\omega-\omega_0)^2/2}$ | $\frac{4\pi s}{\omega_0+\sqrt{2+\omega_0^2}}$ ($1.033s$ for $\omega_0=6$) | $\sqrt2s$ |
| Paul, $m$ | $\frac{2^mi^mm!}{\sqrt{\pi(2m)!}}(1-it)^{-(m+1)}$ | $\frac{2^m}{\sqrt{m(2m-1)!}}\mathcal H(\omega)(s\omega)^me^{-s\omega}$ | $\frac{4\pi s}{2m+1}$ | $s/\sqrt2$ |
| DoG, $m$ | $\frac{(-1)^{m+1}}{\sqrt{\Gamma(m+1/2)}}\frac{d^m}{dt^m}e^{-t^2/2}$ | $-\frac{i^m}{\sqrt{\Gamma(m+1/2)}}(s\omega)^me^{-(s\omega)^2/2}$ | $\frac{2\pi s}{\sqrt{m+1/2}}$ | $\sqrt2s$ |

The "Mexican hat" is DoG with $m=2$: $\psi=\frac2{\sqrt3\pi^{1/4}}(1-t^2)e^{-t^2/2}$. Morlet with $\omega_0=6$ is only approximately admissible (the residual is $\sim e^{-\omega_0^2/2}$); a strictly admissible variant: $\pi^{-1/4}(e^{i\omega_0t}-e^{-\omega_0^2/2})e^{-t^2/2}$.

**FFT algorithm** (T&C, Eq. 4): $X_k=\text{DFT}(x)$, $\omega_k=\pm2\pi k/(N\delta t)$; $W_n(s)=\sum_kX_k\Psi^*(s\omega_k)e^{i\omega_kn\delta t}$ with the normalization $\Psi\leftarrow\sqrt{2\pi s/\delta t}\,\Psi_0$. One FFT of the input and one IFFT per scale: $O(N_sN\log N)$. Scales $s_j=s_02^{j\,\delta j}$, $s_0\approx2\delta t$; for audio $\delta j=1/12..1/48$ (voices per octave). Pad to a power of two $\ge2N$ to prevent circular wrap-around. **Cone of influence** (COI): the zone $|b-\text{edge}|<\tau_s(a)$ is distorted by the edges — hatch it. Scale ↔ frequency: $f=1/\lambda(s)$. Approximate inversion (T&C, Eq. 11): $x_n=\frac{\delta j\,\delta t^{1/2}}{C_\delta\,\psi_0(0)}\sum_j\frac{\operatorname{Re}W_n(s_j)}{s_j^{1/2}}$, $C_\delta=0.776$ for Morlet with $\omega_0=6$, $\psi_0(0)=\pi^{-1/4}$.

**Traps.** Morlet is inadmissible at small $\omega_0$; analytic wavelets need a one-sided $\Psi$ (complex $W$, its modulus is the envelope); aliasing at small scales ($\Psi(s\omega)$ is not small near Nyquist); memory: 10 s at 44.1 kHz × 200 scales × 16 bytes $\approx1.4$ GB — compute over the visible window and decimate in time for display.

**Check.** A sinusoid gives a ridge at the $s$ where $1/\lambda(s)=f$; inversion with a relative error of a few percent; the per-scale energy of white noise is flat with $L^2$ normalization.

### 11.7. Synchrosqueezing [v2] {#synchrosqueezing}

(Daubechies–Lu–Wu 2011.) For an analytic $\psi$, the instantaneous angular frequency is $\omega_x(a,b)=\operatorname{Re}\big(-i\,\partial_bW_x/W_x\big)$ where $|W_x|>\gamma$ (divide by $2\pi$ for Hz); $\partial_bW$ is computed by the same FFT with $\Psi$ multiplied by $i\omega$ (one extra IFFT per scale). Reassignment: $T_x(\omega_l,b)=\sum_{a_k:|\omega_x(a_k,b)-\omega_l|\le\Delta\omega/2}W_x(a_k,b)a_k^{-3/2}\Delta a_k$. Reconstructing a mode within a band: $x(b)\approx\operatorname{Re}\big[C_\psi^{-1}\sum_{l\in\text{band}}T_x(\omega_l,b)\Delta\omega\big]$, $C_\psi=\tfrac12\int_0^\infty\Psi^*(\xi)\frac{d\xi}\xi$. Sharp localization of AM–FM components while invertibility is preserved; the threshold $\gamma$ (a few noise $\sigma$) is critical.

**Check.** On a chirp the ridge follows the true instantaneous frequency; reconstruction error.

### 11.8. Wavelet scattering (Mallat) [research] {#scattering}

$S_0x=x*\phi_J$, $S_1x(\lambda_1)=|x*\psi_{\lambda_1}|*\phi_J$, $S_2x(\lambda_1,\lambda_2)=\big||x*\psi_{\lambda_1}|*\psi_{\lambda_2}\big|*\phi_J$ ($\lambda_2$ coarser than $\lambda_1$): invariance to shifts up to $2^J$, Lipschitz stability to small deformations, energy conservation across orders. For audio: a Morlet bank with $Q\approx8..16$ at the first order and $Q=1$ at the second; $S_1$ is close to a mel spectrogram. Implemented on the FFT banks of the CWT.

### 11.9. STFT / Gabor for comparison [v1] {#stft}

$X(m,k)=\sum_nx[n]w[n-mR]e^{-2\pi ikn/K}$; the resolution is set by the window length $N_w$: $\Delta t\approx N_w/f_s$, $\Delta f\sim f_s/N_w$ (times the main-lobe width factor of the window); the Gaussian window attains the minimum $\sigma_t\sigma_\omega=\tfrac12$. PR: the COLA condition $\sum_mw[n-mR]=\text{const}$ for OLA synthesis, or WOLA $\sum_mw_a[n-mR]w_s[n-mR]=\text{const}$ (a periodic Hann with 50% overlap satisfies COLA, its square root satisfies WOLA). The STFT is a uniform modulated bank with redundancy $N_w/R$. Versus the DWT: uniform tiling versus constant Q; better for harmonics, worse for low-frequency transients; the DWT is critically sampled. The app shows both on the same signal.

**Check.** COLA for the chosen window/hop pair; PR of STFT→ISTFT to $10^{-13}$.

---

## 12. Audio applications [v1 / v2] {#audio}

### 12.1. Denoising [v1] {#audio-denoising}

**Model.** $y=x+\sigma e$, where $e$ is white Gaussian noise. An orthonormal DWT keeps the noise white with the same $\sigma$.

**Noise estimate:** $\hat\sigma=\operatorname{median}(|d_1[k]|)/0.6745$ from the finest level ($0.6745=\Phi^{-1}(3/4)$ is the median of $|N(0,1)|$); robust to a sparse signal. For biorthogonal banks the noise variance at level $j$ is $\sigma^2\|h^{eq}_{1,j}\|^2$ — scale the thresholds by the norms. For colored noise, use a separate $\hat\sigma_j$ at each level.

**Thresholds.**

- VisuShrink: $\lambda=\sigma\sqrt{2\ln N}$ — the noise is removed with high probability, the signal is oversmoothed.
- SureShrink (Donoho–Johnstone 1995): at level $j$ (coefficients normalized by $\sigma$) minimize $\mathrm{SURE}(\lambda)=N_j-2\#\{k:|y_k|\le\lambda\}+\sum_k\min(|y_k|,\lambda)^2$ over the candidates $\lambda\in\{0\}\cup\{|y_k|:|y_k|\le\sqrt{2\ln N_j}\}$ (SURE increases between the nodes, so the minimum is at a node), in $O(N_j\log N_j)$ with sorting and prefix sums; hybrid: if $s^2=(\sum y_k^2-N_j)/N_j\le(\log_2N_j)^{3/2}/\sqrt{N_j}$, take the universal threshold.
- BayesShrink (Chang–Yu–Vetterli 2000): $\lambda=\sigma^2/\hat\sigma_X$, $\hat\sigma_X=\sqrt{\max(\hat\sigma_Y^2-\sigma^2,0)}$, $\hat\sigma_Y^2=\frac1{N_j}\sum y_k^2$; if $\hat\sigma_X=0$, then $\lambda=\max|y_k|$ (suppress the band).

**Shrinkage functions.** Hard $\eta_H=y\,\mathbf 1\{|y|>\lambda\}$; soft $\eta_S=\operatorname{sign}(y)(|y|-\lambda)_+$; non-negative garrote (Gao 1998) $\eta_G=(y-\lambda^2/y)\mathbf 1\{|y|>\lambda\}$; firm (Gao–Bruce 1997), $\lambda_1<\lambda_2$: $0$ for $|y|\le\lambda_1$, $\operatorname{sign}(y)\lambda_2\frac{|y|-\lambda_1}{\lambda_2-\lambda_1}$ for $\lambda_1<|y|\le\lambda_2$, $y$ for $|y|>\lambda_2$. Soft is continuous but biases large coefficients by $\lambda$; hard has less bias but more variance and "musical noise"; garrote and firm are a compromise. The approximation $a_J$ is usually left untouched.

**Cycle spinning** (Coifman–Donoho 1995): $\hat x=\operatorname{avg}_sS_{-s}D(S_sy)$; over all $N$ shifts via the SWT in $O(NLJ)$; removes pseudo-Gibbs artifacts at transients; equivalent to thresholding in the SWT domain (with periodic edges).

**Audio specifics.** Block processing of long files ($2^k$ frames with overlap and stitching), musical noise, a user threshold multiplier of $0.5..1$, preserving attacks.

**Check.** The Donoho–Johnstone test signals (Blocks, Bumps, HeaviSine, Doppler) with known $\sigma$: $\hat\sigma$ within 5% for $N=2^{14}$; SNR gain; $\lambda=0$ is the identity; comparison with pywt / `skimage.restoration.denoise_wavelet` at the same settings.

### 12.2. Compression [v1 / v2] {#audio-compression}

Energy compaction; keeping the $k$ largest: for orthonormal banks the error equals the sum of squares of the discarded coefficients (the best $k$-term approximation, by Parseval); for biorthogonal ones, rank by $|c|\cdot\|f^{eq}\|$ (the norms of the synthesis functions). Dead-zone quantization $q=\operatorname{sign}(c)\lfloor|c|/\Delta\rfloor$, reconstruction $(q+\rho\operatorname{sign}q)\Delta$, $\rho\approx\tfrac12$; bits $\approx$ the entropy of the indices; at high rates $D\approx\Delta^2/12$ per nonzero coefficient. Integer 5/3 + an entropy coder gives lossless compression [v2]; psychoacoustic thresholds are [research].

**Check.** The error equals the sum of the discarded squares (orthonormal) to within $10^{-12}$; the rate–distortion curve is monotone.

### 12.3. Per-band equalization and subband zeroing: why aliasing appears [core] {#audio-subband-zeroing}

This is the main function of the old code. With gains $g_j$ on the subbands of a decimated DWT, the system becomes periodically time-varying (§8.11). For two channels, zeroing the details gives
$$\hat X=\tfrac12F_0(z)\big[H_0(z)X(z)+H_0(-z)X(-z)\big]:$$
the term $\tfrac12F_0(z)H_0(-z)X(-z)$ is no longer cancelled. Its magnitude $\tfrac12|F_0(e^{i\omega})H_0(e^{i(\omega+\pi)})|$ is largest in the transition band around $\pi/2$: for Haar it is $\tfrac12|\sin\omega|$ ($-6$ dB at $\pi/2$), for db8 much smaller, but not zero. By ear: content near the band edges is reflected to mirror frequencies (inharmonic tones), the result depends on the position of the signal modulo $2^J$ (time variance), and long synthesis filters cause pre-echo and ringing. Zeroing level $j$ creates aliasing near the edges $\pi/2^j$ and $\pi/2^{j-1}$, with components at frequency $2^j$.

**How to reduce it.**

1. SWT/MODWT MRA: $x=\sum_jD_j+S_J$; zeroing $D_j$ is an LTI filtering with the response $1-H^{\text{MRA}}_j(\omega)$; there is no aliasing, only the wide transition of the equivalent filter remains.
2. Wavelet packets with long (sharp) filters — db20/sym20: aliasing decreases (but does not vanish), the bands are narrower.
3. Smooth gains (a transition from 1 to 0 across neighboring bands) instead of a hard zero.
4. DTCWT — aliasing is substantially suppressed.
5. For a pure equalizer — classical filters (§9) or the STFT.

**Check.** The aliasing energy (§14.6) for each strategy; the plot of $A^{(g)}_k$; for the MRA variant the aliasing is zero up to rounding.

### 12.4. Transient and onset detection [v1] {#audio-onsets}

The energy of the fine levels $e_j[k]=d_j[k]^2$ (SWT for time resolution) with smoothing; the detection function $\mathrm{ODF}[n]=\sum_jw_j\max\big(0,\Delta\log(\varepsilon+E_j[n])\big)$ (a half-wave rectified flux of the band log-energies); peak picking with an adaptive threshold (median $+\delta$) and a minimum interval. Modulus maxima across scales (Mallat–Hwang): singularities produce maxima lines, and the decay $|W|\sim a^{\alpha+1/2}$ estimates the local Hölder exponent $\alpha$ (a click or a step).

**Check.** Synthetic clicks and decaying tones — onset time accurate to within $\pm$ one frame hop; F-measure on an annotated dataset (later).

### 12.5. Band energy features [v1] {#audio-band-energy}

$E_j=\sum_kd_j[k]^2$; fractions $E_j/\sum E$; log-energies; frame by frame (e.g. 2048 samples with a hop of 512) — a "wavelet spectrogram"; for display, divide by the band length (power per sample). For an orthonormal bank with periodization $\sum E_j+\|a_J\|^2=\|x\|^2$.

### 12.6. Stereo [v1] {#audio-stereo}

Per channel — identical filters and levels give aligned coefficients. Mid/side: $M=(L+R)/2$, $S=(L-R)/2$ (inverse: $L=M+S$, $R=M-S$), or orthonormally $(L\pm R)/\sqrt2$. A linear transform commutes with the wavelet transform but not with thresholding: denoise $M$ and $S$ separately (noise often dominates $S$); for correlated noise use a joint threshold on $\sqrt{c_L^2+c_R^2}$; when zeroing subbands, apply the same mask to both channels to preserve the stereo image. WAV formats (int16/24/float, interleaved) are converted to double in $[-1,1)$.

---

## 13. Numerical issues [core] {#numerics}

### 13.1. Formats and precision {#num-formats}

float32: $u=2^{-24}\approx6\cdot10^{-8}$; float64: $u=2^{-53}\approx1.1\cdot10^{-16}$; x87 long double ($2^{-64}$) is not portable (absent on ARM and MSVC); references: `__float128`, Boost.Multiprecision, MPFR. Recommendation: double for processing and design; float32 only for display buffers and the real-time path, with a documented error.

### 13.2. Error growth across levels {#num-error-growth}

One convolution: $|\mathrm{fl}(y)-y|\le\gamma_L\sum|h||x|$, $\gamma_L\approx Lu$. One analysis + synthesis level adds $\approx cLu\|x\|\kappa$. Orthonormal banks do not amplify errors: the growth is linear in $J$ in the worst case and $\sqrt J$ statistically. Biorthogonal banks amplify by the synthesis norms; the condition number of the multilevel matrix is 2.0…3.2 (`bior2.2`, $J=1..7$), 1.3…1.5 (9/7), 4…50 (`bior3.1`, verified). Expected PR error in double: $10^{-15}..10^{-14}$ relative at $J=10$ (orthogonal); in float32, $\sim10^{-6}$. Test tolerance: $100\,J\,L\,u\,\|x\|_\infty$.

Compensated (Kahan–Neumaier) or pairwise summation for energies (Parseval over $10^7$ samples with a naive sum gives an error of $\sim Nu$). FMA changes results bitwise between compilers and processors — tests use tolerances, not bitwise equality (except for integer lifting and exact rational arithmetic). Denormals in decaying IIR filters: enable FTZ/DAZ in the real-time path.

### 13.3. Exact arithmetic as a reference {#num-exact-reference}

Rational arithmetic (Boost.Multiprecision `cpp_rational` or GMP `mpq`) for filters with rational coefficients in sum-to-one normalization: Haar, LeGall 5/3, the spline CDF family (`bior1.x`, `2.x`, `3.x` — the coefficients of $B_p$ are integers). With integer input DWT/IDWT are exact, and the reconstruction must match bit for bit; the double implementation is compared against this reference with a tolerance. Irrational filters (dbN): coefficients from spectral factorization in MPFR at 200–300 bits; for D4, exact arithmetic in $\mathbb Q(\sqrt3)$ is possible (pairs $a+b\sqrt3$).

### 13.4. Conditioning of biorthogonal pairs {#num-conditioning}

$\kappa(A)=\sup_\omega\sigma_{\max}(H_p)/\inf_\omega\sigma_{\min}(H_p)$ with $\sqrt2$ normalization of both lowpass filters. Computed: Haar and dbN — 1; `bior1.3` — 1.28; 9/7 — 1.32; `bior6.8` — 1.33; 5/3 — 2.0; `bior3.1` and `bior3.3` — 4.0; the non-wavelet factorizations of §4.8 — up to 10.9. The noise of quantized coefficients in synthesis is proportional to $\|f^{eq}\|$ — this matters for compression and denoising.

### 13.5. Root finding for spectral factorization {#num-root-finding}

The eigenvalues of the companion matrix (LAPACK `dhseqr`, Eigen) are backward stable for the polynomial, but multiple and clustered roots get an error of $\sim u^{1/m}$. Remedies: remove the known roots ($-1$) exactly; search in the variable $y=\sin^2(\omega/2)$ (degree $p-1$); refine with Newton on the original polynomial; or **Aberth–Ehrlich**, a simultaneous iteration
$$z_k\leftarrow z_k-\frac{w_k}{1-w_k\sum_{j\ne k}\frac1{z_k-z_j}},\qquad w_k=\frac{p(z_k)}{p'(z_k)},$$
with cubic convergence, all roots at once, no deflation; initial guesses lie on a circle whose radius comes from the Cauchy/Fujiwara bounds (this is how MPSolve works, with adaptive precision). Deflating one root at a time accumulates errors; if it is needed, remove the roots in increasing order of modulus (forward deflation is stable for small roots) and refine each one against the original polynomial. After factorization: assemble the product (conjugate pairs as real quadratic factors, multiplied in increasing order of modulus), compare it with $P$ (relative $10^{-13}$), renormalize.

### 13.6. Determinism and performance {#num-determinism}

SIMD convolutions (AVX2/NEON) with a fixed accumulation order for reproducibility; an FFT library (pocketfft / FFTW / KissFFT) with plan caching; threads per channel and per level; no denormals.

---

## 14. Quality metrics [core] {#metrics}

### 14.1. Reconstruction error {#met-reconstruction}

$e=x-\hat x$: $\max|e|$, $\mathrm{RMS}=\sqrt{\overline{e^2}}$, relative $\|e\|_2/\|x\|_2$; for precision tests, in units in the last place (ulp).

### 14.2. SNR, PSNR, SegSNR {#met-snr}

$\mathrm{SNR}=10\lg\frac{\sum x^2}{\sum e^2}$ dB; $\mathrm{PSNR}=10\lg\frac{\text{peak}^2}{\mathrm{MSE}}$, peak $=1$ (float, full scale) or $32767$ (int16) — state it explicitly; $\mathrm{SegSNR}=\frac1M\sum_m\operatorname{clamp}\big(10\lg\frac{\sum_{\text{frame }m}x^2}{\sum_{\text{frame }m}e^2},-10,35\big)$ dB over 15–30 ms frames, skipping silent frames. For denoising, the gain $\Delta\mathrm{SNR}$.

### 14.3. Parseval and Riesz bounds {#met-parseval}

Orthonormal bank with periodization: $\sum x^2=\sum_j\sum_kd_j[k]^2+\sum a_J^2$ up to rounding (relative $10^{-14}$ with compensated summation). Other boundary modes: no equality. Biorthogonal: $A\|x\|^2\le\sum|c|^2\le B\|x\|^2$ with the bounds from §6.4 — check the inequality.

### 14.4. Coding gain {#met-coding-gain}

For an orthonormal bank with band fractions $\alpha_k=N_k/N$ and variances $\sigma_k^2$: $G=\dfrac{\sum_k\alpha_k\sigma_k^2}{\prod_k(\sigma_k^2)^{\alpha_k}}$ (the ratio of the arithmetic and geometric means). Reference: AR(1), $\rho=0.95$: $\sigma_k^2=\frac1{2\pi}\int S_x(\omega)|H^{eq}_k(e^{i\omega})|^2d\omega$, $S_x=\sigma_w^2/|1-\rho e^{-i\omega}|^2$; the limit $G_\infty=\sigma_x^2/\exp\big(\frac1{2\pi}\int\ln S_x\big)=1/(1-\rho^2)\approx10.1$ dB. Show $G$ for each family and number of levels.

### 14.5. Frequency-domain PR checks {#met-frequency-checks}

$\max_\omega\big||T(e^{i\omega})|-1\big|$ and $\max_\omega|A(e^{i\omega})|$ on an 8192-point grid — expected $\le10^{-14}$; the power-complementarity defect for orthogonal banks; the plots of §8.

### 14.6. Aliasing energy and shift variance {#met-alias-energy}

For a modified bank: $E_{\text{alias}}=\dfrac{\sum_{k\ne0}\frac1{2\pi}\int|A_k|^2d\omega}{\frac1{2\pi}\int|A_0|^2d\omega}$ — for a white input, the ratio of the aliasing power to the power of the LTI part. For a specific input: $\hat x_{\text{alias}}=\hat x-a_0*x$ ($a_0$ is the impulse response of $A_0$), its energy relative to $\|\hat x\|^2$ in dB. Shift variance: $\max_s\|S_{-s}\Phi(S_sx)-\Phi(x)\|/\|\Phi(x)\|$.

### 14.7. Other metrics {#met-other}

Log-spectral distance (LSD), spectral flatness; perceptual metrics (PEAQ, ViSQOL) — [research].

---

## 15. Consolidated test plan [core] {#test-plan}

1. **Filters:** coefficients = pywt (after trimming zeros, with the offset); the number of zeros at $-1$ by exact division; orthogonality $\sum h[n]h[n+2k]=\delta_k$; linear phase — symmetry and group delay $M/2$; the $-3$ dB point of an orthogonal lowpass exactly at $\pi/2$.
2. **Bank PR:** $T=z^{-\ell}$, $A=0$ as polynomials; $\det H_p$ is a monomial; $F_pH_p=I$; the table of §4.8 in full ($P_0$, $s$, $\kappa$, cascade growth).
3. **Matrices:** $T_hx$ = convolution; the spectrum of a circulant = FFT; the spectrum of the periodized analysis matrix = SVD of $H_p$ on a grid; $WW^{\mathsf T}=I$ for Haar with $N=8$ and for db3; DCT-II diagonalizes the HS "Toeplitz + Hankel".
4. **Boundaries:** the 9 pywt modes — PR, lengths, coefficient agreement; non-expansive symmetric — $N$ coefficients and PR for odd and even $N$; $N<L$ and the maximum level.
5. **Transforms:** DWT/IDWT = pywt; SWT — equivariance, MODWT energy, $\sum D_j+S_J=x$; packets — PR from any basis, best basis versus exhaustive search; lifting = convolution, integer lifting — a bit-exact round trip; CWT — the ridge of a sinusoid and approximate inversion.
6. **Frequency analysis:** group delay by the ratio formula = $M/2$ for symmetric filters; $\sum|\Phi(\omega+2\pi k)|^2=1$, $\sum_j|\Psi(2^j\omega)|^2=1$; the theoretical $A_k$ = those measured from impulse responses.
7. **Applications:** $\hat\sigma$ by MAD on test signals; SURE threshold versus exhaustive search; aliasing energy under zeroing: DWT > packets with long filters > MRA (≈0).
8. **Numerics:** a rational-arithmetic reference for Haar, 5/3 and `bior*`; an MPFR reference for dbN; tolerances $100\,JLu\|x\|_\infty$; Parseval with compensated summation.
9. **Resampling:** 160/147 — the level of images and aliasing below the specification; $L/L$ is the identity; comparison with `resample_poly`.
10. **Explain mode:** all numbers in §5.2–5.4 are reproduced exactly (sum-to-one normalization).

---

## References {#references}

All sources have been verified by search (existence and year), except those marked otherwise.

**Books**

- G. Strang, T. Nguyen. *Wavelets and Filter Banks*. Wellesley-Cambridge Press, 1996.
- S. Mallat. *A Wavelet Tour of Signal Processing: The Sparse Way*, 3rd ed. Academic Press/Elsevier, 2009 (with contributions by G. Peyré).
- I. Daubechies. *Ten Lectures on Wavelets*. CBMS-NSF Regional Conference Series in Applied Mathematics 61, SIAM, 1992.
- M. Vetterli, J. Kovačević. *Wavelets and Subband Coding*. Prentice Hall, 1995 (later made freely available by the authors).
- M. Vetterli, J. Kovačević, V. K. Goyal. *Foundations of Signal Processing*. Cambridge University Press, 2014.
- P. P. Vaidyanathan. *Multirate Systems and Filter Banks*. Prentice Hall, 1993.
- D. B. Percival, A. T. Walden. *Wavelet Methods for Time Series Analysis*. Cambridge University Press, 2000.
- R. E. Crochiere, L. R. Rabiner. *Multirate Digital Signal Processing*. Prentice-Hall, 1983.
- A. V. Oppenheim, R. W. Schafer. *Discrete-Time Signal Processing*, 3rd ed. Pearson, 2010.
- R. M. Gray. Toeplitz and Circulant Matrices: A Review. *Foundations and Trends in Communications and Information Theory* 2(3), 155–239, 2006.

**Articles**

- A. Croisier, D. Esteban, C. Galand. Perfect channel splitting by use of interpolation/decimation/tree decomposition techniques. Int. Conf. on Information Sciences and Systems, Patras, 1976. *Not verified: only secondary references were found.*
- M. J. T. Smith, T. P. Barnwell III. Exact reconstruction techniques for tree-structured subband coders. *IEEE Trans. ASSP* 34(3), 434–441, 1986.
- S. G. Mallat. A theory for multiresolution signal decomposition: the wavelet representation. *IEEE Trans. PAMI* 11(7), 674–693, 1989.
- A. Cohen, I. Daubechies, J.-C. Feauveau. Biorthogonal bases of compactly supported wavelets. *Comm. Pure Appl. Math.* 45(5), 485–560, 1992.
- L. F. Villemoes. Energy moments in time and frequency for two-scale difference equation solutions and wavelets. *SIAM J. Math. Anal.* 23, 1519–1543, 1992. (Existence verified; the exact statement of the theorem on the Sobolev exponent was not checked against the full text — the formula of §4.8 was verified numerically against known values.)
- R. R. Coifman, M. V. Wickerhauser. Entropy-based algorithms for best basis selection. *IEEE Trans. Inf. Theory* 38(2), 713–718, 1992.
- A. Cohen, I. Daubechies, P. Vial. Wavelets on the interval and fast wavelet transforms. *Appl. Comput. Harmon. Anal.* 1(1), 54–81, 1993.
- W. Verhelst, M. Roelands. An overlap-add technique based on waveform similarity (WSOLA) for high quality time-scale modification of speech. ICASSP, 1993.
- D. L. Donoho, I. M. Johnstone. Ideal spatial adaptation by wavelet shrinkage. *Biometrika* 81(3), 425–455, 1994.
- D. L. Donoho, I. M. Johnstone. Adapting to unknown smoothness via wavelet shrinkage. *JASA* 90(432), 1200–1224, 1995.
- R. R. Coifman, D. L. Donoho. Translation-invariant de-noising. In: *Wavelets and Statistics*, Lecture Notes in Statistics 103, Springer, 1995.
- W. Sweldens. The lifting scheme: a custom-design construction of biorthogonal wavelets. *Appl. Comput. Harmon. Anal.* 3, 1996.
- H.-Y. Gao, A. G. Bruce. WaveShrink with firm shrinkage. *Statistica Sinica* 7(4), 855–874, 1997.
- I. Daubechies, W. Sweldens. Factoring wavelet transforms into lifting steps. *J. Fourier Anal. Appl.* 4(3), 247–269, 1998.
- A. R. Calderbank, I. Daubechies, W. Sweldens, B.-L. Yeo. Wavelet transforms that map integers to integers. *Appl. Comput. Harmon. Anal.* 5(3), 332–369, 1998.
- H.-Y. Gao. Wavelet shrinkage denoising using the non-negative garrote. *J. Comput. Graph. Stat.* 7(4), 469–488, 1998.
- C. Torrence, G. P. Compo. A practical guide to wavelet analysis. *Bull. Amer. Meteor. Soc.* 79(1), 61–78, 1998.
- J. Laroche, M. Dolson. Improved phase vocoder time-scale modification of audio. *IEEE Trans. Speech Audio Process.* 7(3), 323–332, 1999.
- S. G. Chang, B. Yu, M. Vetterli. Adaptive wavelet thresholding for image denoising and compression. *IEEE Trans. Image Process.* 9(9), 1532–1546, 2000.
- N. Kingsbury. Complex wavelets for shift invariant analysis and filtering of signals. *Appl. Comput. Harmon. Anal.* 10, 234–253, 2001.
- I. Daubechies, J. Lu, H.-T. Wu. Synchrosqueezed wavelet transforms: an empirical mode decomposition-like tool. *Appl. Comput. Harmon. Anal.* 30, 243–261, 2011.
- S. Mallat. Group invariant scattering. *Comm. Pure Appl. Math.* 65(10), 1331–1398, 2012.
- G. R. Lee, R. Gommers, F. Wasilewski, K. Wohlfahrt, A. O'Leary. PyWavelets: A Python package for wavelet analysis. *J. Open Source Software* 4(36), 1237, 2019. (On the JOSS page the third author's surname is spelled "Waselewski", in the PyWavelets documentation "Wasilewski".)
- T. W. Parks, J. H. McClellan. Chebyshev approximation for nonrecursive digital filters with linear phase. *IEEE Trans. Circuit Theory* 19(2), 189–194, 1972.
- O. Herrmann. On the approximation problem in nonrecursive digital filter design. *IEEE Trans. Circuit Theory* 18(3), 411–413, 1971.
- O. Aberth. Iteration methods for finding all zeros of a polynomial simultaneously. *Math. Comp.* 27(122), 339–344, 1973.

Mentioned in the text without a separate search check (well-known results, cited from the textbooks above): the Vaidyanathan lattice for paraunitary matrices, the Vaidyanathan–Nguyen trick for half-band filters, the Mallat–Hwang modulus maxima, Bauer's method, Szegő, Gohberg–Semencul (the formula was verified numerically), Cybenko on the stability of Levinson, Johnstone–Silverman on level-dependent noise.
