+++
id = "stft"
title = "Спектрограмма STFT"
theory = ["stft", "freq-heisenberg"]
source = "src/core/spectral.cpp"
function = "stft_frames"
test = "tests/core/test_spectral.cpp"
views = ["spectrum"]
+++

## Назначение

Показать, как частотное содержание сигнала меняется во времени, и почувствовать компромисс «время — частота»: длинное окно различает близкие тоны, короткое — короткие удары.

## Формулы

Кадр $m$ с шагом $R$ и окном Ханна $w$ длины $N$:

$$X(m,k)=\sum_{n=0}^{N-1} x[n+mR]\,w[n]\,e^{-2\pi i k n/N}, \qquad w[n]=\tfrac12-\tfrac12\cos(2\pi n/N)$$

Мощность в шкале «спектр», как `scipy.signal.stft`: $P(m,k)=\lvert X(m,k)\rvert^2/\big(\sum_n w[n]\big)^2$.

Разрешение: $\Delta t = N/f_s$, $\Delta f = f_s/N$; их произведение не меньше $1/4\pi$ (§8.10).

## Алгоритм и стоимость

1. Окно Ханна — периодическое, как у scipy; шаг $R = N/4$ (перекрытие 75 %).
2. Каждый кадр умножается на окно, вещественное БПФ даёт $N/2+1$ бинов.
3. Кадры сворачиваются в столбцы экрана средней мощностью, бины — в строки (лог. или лин. ось).

Стоимость $O(M\,N\log N)$ для $M$ кадров; память — один кадр плюс свёрнутые столбцы.

## С числами этого блока

Окно $N$ = {{block.n}}, шаг {{block.hop}} (перекрытие {{block.overlap}}), $f_s$ = {{block.rate}}.

Разрешение: $\Delta t$ = {{metric.dt}}, $\Delta f$ = {{metric.df}}; столбцов кадров: {{metric.frames}}.

## Проверки

- Мощность кадров совпадает со `scipy.signal.stft` до $10^{-12}$ от максимума.
- Сетка экрана для $N = 512$ и $N = 4096$ совпадает с данными холста до 1 дБ.

## Связи

- §11.9 — STFT и условие COLA.
- §8.10 — прямоугольники Гейзенберга.
- §9.3 — окна и утечка.

## Реализация

Кадр: окно, вещественное БПФ, мощность в шкале «спектр».

```cpp
        for (std::size_t k = 0; k < n; ++k) buffer[k] = x[f * hop + k] * window[k];
        pocketfft::r2c(shape, real_stride, complex_stride, 0, pocketfft::FORWARD, buffer.data(), spectrum.data(), 1.0);
        for (std::size_t b = 0; b < bins; ++b) power[b] = std::norm(spectrum[b]) * scale;
        out(f, power);
```
