+++
id = "cepstrum"
title = "Кепстр и DCT"
theory = ["toeplitz-plus-hankel", "fb-spectral-factorisation", "lti-z-dtft"]
source = "src/core/analysis.cpp"
function = "real_cepstrum"
test = "tests/core/test_analysis.cpp"
views = ["under_the_hood"]
+++

## Назначение

Два преобразования «спектра спектра»: `cepstrum` — вещественный кепстр, где период голоса или эха виден как пик на оси задержки в отсчётах (квефренции), `dct` и `idct` — ортонормированное косинусное преобразование и обратное к нему, на котором стоят MFCC и сжатие.

## Формулы

Вещественный кепстр всего сигнала длины $N$ — обратное DFT натурального логарифма модуля спектра с полом $\varepsilon$:

$$c[n] = \mathrm{Re}\,\frac{1}{N}\sum_{k=0}^{N-1} \ln\big(\lvert X[k]\rvert + \varepsilon\big)\,e^{2\pi i k n/N}, \qquad \varepsilon = 10^{-12}\max_k \lvert X[k]\rvert$$

Логарифм берётся от модуля, а не от мощности и не в дБ; для тишины ($\max\lvert X\rvert = 0$) выход — нули. Периодичность $T_0$ даёт пик при $n = T_0 f_s$.

`dct` — DCT-II с ортонормировкой, как `scipy.fft.dct(x, type=2, norm="ortho")`:

$$X_k = \lambda_k\sqrt{\frac{2}{N}}\sum_{n=0}^{N-1} x[n]\cos\frac{\pi k(2n+1)}{2N}, \qquad \lambda_0 = \frac{1}{\sqrt2}, \quad \lambda_{k>0} = 1$$

`idct` — транспонированная матрица, то есть DCT-III с той же нормировкой:

$$x[n] = \sqrt{\frac{2}{N}}\sum_{k=0}^{N-1} \lambda_k X_k \cos\frac{\pi k(2n+1)}{2N}$$

Матрица ортогональна: $\sum_n x[n]^2 = \sum_k X_k^2$ и `idct` от `dct` возвращает сигнал.

## Алгоритм и стоимость

1. `cepstrum`: комплексное БПФ длины $N$, максимум модуля для пола, логарифм, обратное БПФ, действительная часть. Кадров нет — кепстр считается по всему каналу сразу.
2. `dct` и `idct`: быстрые DCT-II и DCT-III из pocketfft с множителем $1/\sqrt{2N}$ и ортонормировкой нулевого коэффициента.
3. Длина и частота дискретизации выхода — как у входа.

Стоимость $O(N\log N)$ на канал; память $O(N)$.

## С числами этого блока

Параметров нет: длина выхода равна длине входа $N$. Время узла {{block.wall}}, память {{block.bytes}}.

## Проверки

- `real_cepstrum` совпадает с `numpy`-эталоном `real(ifft(log(abs(fft(x)) + 1e-12 * max)))` до $10^{-9}$ для длин 37, 64 и 1.
- `dct2` совпадает со `scipy.fft.dct(type=2, norm="ortho")` до $10^{-12}$, `idct2` от `dct2` возвращает сигнал до $10^{-12}$; матрица DCT-II размера 9 ортогональна и совпадает с быстрым преобразованием до $10^{-14}$.
- В конвейере `idct` от `dct` восстанавливает второй канал стерео до $10^{-12}$, блок `cepstrum` равен ядру по каналам, кепстр тишины — ровно нули.

## Связи

- §2.4 — Тёплиц плюс Ганкель: DCT-II диагонализирует симметричное продолжение.
- §4.7 — спектральная факторизация: кепстральный метод Колмогорова тоже идёт через $\log$ спектра и обратное БПФ.
- §1.2 — z-преобразование и DTFT.
- Карточка `spectral_features`: MFCC — это DCT логарифма мел-энергий.

## Реализация

Логарифм модуля спектра с полом от максимума и обратное БПФ.

```cpp
    auto spectrum = fft(std::vector<C>(x.begin(), x.end()), true);
    double peak = 0;
    for (const auto& v : spectrum) peak = std::max(peak, std::abs(v));
    if (!(peak > 0)) return std::vector<double>(x.size(), 0.0);
    for (auto& v : spectrum) v = std::log(std::abs(v) + 1e-12 * peak);
    const auto back = fft(std::move(spectrum), false);
```
