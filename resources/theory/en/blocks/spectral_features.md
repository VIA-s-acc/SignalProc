+++
id = "spectral_features"
title = "Spectral features"
theory = ["stft", "audio-onsets", "audio-band-energy", "met-other"]
source = "src/pipeline/blocks_analysis.cpp"
function = "spectral_frame"
test = "tests/pipeline/test_blocks_analysis.cpp"
views = ["spectrum"]
+++

## Purpose

Reduce a spectrum to a few numbers: `mfcc` gives mel-cepstral coefficients, `spectral_features` the centroid, spread, rolloff, flatness and flux, `spectrum_peaks` the most prominent peaks of the Welch spectrum, `band_power` the power shares per octave or third-octave, `thd` the harmonic distortion of a tone, `onsets` the attack times.

## Formulas

Frames of `mfcc` and `spectral_features`: a periodic Hann window $w$ of length $N$, step $R$, no zero padding; power on the "spectrum" scale $P(k) = \lvert X(k)\rvert^2 / \big(\sum_n w[n]\big)^2$ at frequencies $f_k = k f_s/N$, $k = 0, \dots, N/2$.

MFCC: the HTK mel scale $\mathrm{mel}(f) = 2595\log_{10}(1 + f/700)$, $M + 2$ edges $\phi_j$ evenly spaced in mel from 0 to $\mathrm{mel}(f_s/2)$, triangles with peak 1 (the area is not normalised), an orthonormal DCT-II as in `scipy.fft.dct(norm="ortho")`:

$$H_j(f) = \max\Big(0,\ \min\Big(\frac{f - \phi_j}{\phi_{j+1} - \phi_j},\ \frac{\phi_{j+2} - f}{\phi_{j+2} - \phi_{j+1}}\Big)\Big), \qquad L_j = 10\log_{10}\max\Big(\sum_k H_j(f_k)\,P(k),\ 10^{-10}\Big)$$

$$c_q = \sqrt{\frac{2}{M}}\,\lambda_q\sum_{j=0}^{M-1} L_j\cos\frac{\pi q\,(2j+1)}{2M}, \qquad \lambda_0 = \frac{1}{\sqrt2}, \quad \lambda_{q \ge 1} = 1$$

Frame features use the magnitudes $A_k = \sqrt{P(k)}$; the rolloff is the smallest $f_k$ with $\sum_{j \le k} A_j \ge \frac{p}{100}\sum_j A_j$ (weights are magnitudes, not powers):

$$\mu = \frac{\sum_k f_k A_k}{\sum_k A_k}, \qquad \sigma = \sqrt{\frac{\sum_k (f_k - \mu)^2 A_k}{\sum_k A_k}}, \qquad \Delta_m = \sqrt{\sum_k \big(A_{m,k} - A_{m-1,k}\big)^2}$$

Flatness uses the power with a floor $\tilde P_k = \max(P(k), 10^{-10})$ over all $K = N/2 + 1$ bins: $\Phi = \exp\big(\frac1K\sum_k\ln\tilde P_k\big) \big/ \big(\frac1K\sum_k\tilde P_k\big)$. The flux $\Delta_m$ is not rectified: rising and falling magnitudes count alike.

The Welch spectrum of `spectrum_peaks` and `band_power`, as in `scipy.signal.welch`: segment $L = \min(\text{length}, \text{segment})$, step $L - \lfloor L/2 \rfloor$, a Hann window, each of the $G$ segments has its mean removed, $c_k = 2$ except bins 0 and $L/2$:

$$S(k) = \frac{c_k}{G\,f_s\sum_n w[n]^2}\sum_{s=1}^{G}\Big\lvert\sum_n \big(x_s[n] - \bar x_s\big)\,w[n]\,e^{-2\pi i k n/L}\Big\rvert^2, \qquad D(k) = 10\log_{10}\max\big(S(k), 10^{-20}\big)$$

Peaks are local maxima of $D$ (the middle of a plateau) with a prominence as in `scipy.signal.find_peaks`: $\pi_k = D(k) - \max(m_l, m_r)$, where $m_l, m_r$ are the minima of $D$ to the left and right up to the first point above $D(k)$. A peak's frequency is the bin $k f_s/L$, with no interpolation.

Bands are base-ten, $s = 3$, $i_0 = -5$ for octaves (centres 31.5 Hz … 16 kHz) and $s = 1$, $i_0 = -16$ for thirds (25 Hz … 20 kHz); the first edge is 22.4 Hz, the last 22.4 kHz:

$$g_i = 1000\cdot 10^{(s\,(i + i_0) - s/2)/10}, \qquad p_b = \frac{\sum_{g_b \le f_k < g_{b+1}} S(k)}{\sum_k S(k)}$$

THD: one FFT of the whole signal of length $N$ with a 4-term Blackman–Harris window, $\theta = 2\pi n/N$; the peak $k^*$ of the power $P_k = \lvert X_k\rvert^2$ is refined by a parabola through $\ln P$ (the shift is clamped to $\pm\tfrac12$), and the power $B_h$ of harmonic $h$ is the sum of $\pm 4$ bins around $[h\hat k]$, where $[\cdot]$ rounds to an integer:

$$w[n] = 0.35875 - 0.48829\cos\theta + 0.14128\cos 2\theta - 0.01168\cos 3\theta, \qquad \hat k = k^* + \frac{a - c}{2\,(a - 2b + c)}$$

$$\mathrm{THD} = \sqrt{\frac{\sum_{h=2}^{H} B_h}{B_1}}, \qquad f_0 = \frac{\hat k f_s}{N}, \qquad a, b, c = \ln P_{k^*-1},\ \ln P_{k^*},\ \ln P_{k^*+1}$$

Onsets: a mono mix (the mean of the channels), frames without padding, the detection function is the rectified spectral flux with $\lvert X_{-1}\rvert = 0$. Frame $m$ is an onset if it is a local maximum, exceeds the threshold and lies at least $\lceil 0.05 f_s/R\rceil$ frames after the previous onset:

$$\mathrm{SF}(m) = \sum_k \max\big(0,\ \lvert X_m(k)\rvert - \lvert X_{m-1}(k)\rvert\big), \qquad \mathrm{SF}(m) > 1.5\,\mathrm{med}_{\lvert j - m\rvert \le W}\mathrm{SF}(j) + 0.1\max_j\mathrm{SF}(j)$$

The median window is $W = \max\big(1, [0.25 f_s/R]\big)$ frames; the onset time is $t = \max\big(0,\ mR + \tfrac34 N - \tfrac12 R\big)/f_s$: a click $p$ samples into a frame gives a flux maximum where the Hann slope is steepest, $p + R/2 = 3N/4$.

## Algorithm and cost

1. `mfcc` and `spectral_features`: $F = 1 + \lfloor (L_x - N)/R \rfloor$ frames; a signal shorter than $N$ gives 0 frames and zero fields. The report holds means over frames; frames whose magnitudes sum to zero are skipped for $\mu$, $\sigma$, rolloff and $\Phi$, but the flux is taken over all $F - 1$ pairs.
2. The mel triangles are sampled on the bin grid; with a large $M$ and a short $N$ the lowest triangles may miss every bin, and then $L_j = -100$ dB. Unlike librosa: the HTK scale rather than Slaney, no area normalisation and no `top_db` clipping.
3. `spectrum_peaks`: peaks are sorted by $D$ in descending order (stable) and cut to `count`. `band_power`: each bin goes whole to the band of its centre frequency; DC falls into "below 22 Hz"; bands whose lower edge is at or above $f_s/2$ are not listed, and the shares sum to 1.
4. `thd`: the peak is searched among bins $1 \dots N/2 - 1$; harmonics are read at $h\hat k$ with no search for their own maximum and stop when $h\hat k + 4 > N/2$. `harmonics` is $H$, the number of the highest harmonic, the fundamental being the first. An error if the peak is below bin 10 (the bands of the fundamental and the second harmonic would overlap) or no harmonic lies below Nyquist.
5. `onsets`: the median uses `nth_element`, so an even window takes the upper of the two middle values; the window is truncated at the edges; if the flux maximum is at most $10^{-12}$ there are no onsets.

Cost: the STFT blocks $O(F N\log N)$ plus $O(F M N)$ for the mel bank; Welch $O(G L\log L)$; THD $O(N\log N)$; onsets $O(F N\log N + F W)$.

## With this block's numbers

MFCC: {{block.n_mfcc}} coefficients from {{block.n_mels}} mel bands, frame {{block.frame}}, step {{block.hop}}. Spectral features: rolloff at {{block.rolloff_pct}} % of the magnitude sum.

Peaks: at most {{block.count}} with a prominence of at least {{block.prominence_db}} dB, Welch segment {{block.segment}}. Bands: {{block.bands}}. THD: harmonics 2 to {{block.harmonics}}. Onsets: window {{block.n}}, step {{block.hop}}.

The node ran for {{block.wall}} and used {{block.bytes}}.

## Checks

- `mfcc` matches a numpy reference (HTK mel, triangles with peak 1, `scipy.fft.dct(type=2, norm="ortho")`) to $10^{-10}$ for $(Q, M, N, R) = (13, 20, 256, 128)$ and $(8, 40, 512, 256)$, the frame count exactly; $Q > M$ is refused.
- `spectral_features` match a numpy reference to $10^{-10}$ for $(N, R, p) = (256, 64, 85)$ and $(512, 200, 50)$; silence gives a centroid and flatness of 0.
- `spectrum_peaks`: frequencies to $10^{-9}$ Hz and levels to $10^{-10}$ dB match `scipy.signal.welch` with `find_peaks` for segments 4096 and 64. `band_power` matches a reference built on `welch` to $10^{-10}$ for octaves and thirds, the shares sum to 1 within $10^{-12}$; a silent channel gives zero shares and 0 peaks.
- `thd`: a 1 kHz tone with 1 % and 0.1 % harmonics gives $f_0$ within 0.01 Hz, THD within $10^{-3}$ of $\sqrt{0.01^2 + 0.001^2}$, harmonics at −40 and −60 dB within 0.05 dB; a non-integer number of periods (441.7 Hz, 30011 samples) gives $f_0$ within 0.1 Hz and THD within 2 %. A tone that is too low or too short, or has no harmonic below Nyquist, is refused.
- `onsets`: four synthetic clicks are found within $(R/2 + 1)/f_s$; silence and a constant signal give no onsets; a tone from the very start gives no negative times.

## Links

- §11.9 — STFT and the COLA condition.
- §12.4 — onset detection; the book builds the detection function from the log energies of wavelet bands, while `onsets` uses the STFT spectral flux.
- §12.5 — band energy features.
- §14.7 — spectral flatness among the other metrics.
- The "STFT spectrogram" and "Fundamental frequency: YIN" cards.

## Implementation

The features of one frame: spread, rolloff and flatness in one pass over the bins.

```cpp
    for (std::size_t k = 0; k < m.size(); ++k) {
        spread += (freq(k) - centroid) * (freq(k) - centroid) * m[k];
        cumulative += m[k];
        if (!rolloff && cumulative >= pct / 100 * total) rolloff = freq(k);
        const double floored = std::max(power[k], kPowerFloor);
        logs += std::log(floored);
        sum += floored;
    }
```
