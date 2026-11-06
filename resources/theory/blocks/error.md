+++
id = "error"
title = "Ошибка восстановления"
theory = ["met-reconstruction"]
source = "src/core/metrics.cpp"
function = "max_abs_error"
test = "tests/core/test_metrics.cpp"
views = ["decomposition"]
+++

## Назначение

Измерить, насколько восстановленный сигнал $\hat x$ отличается от исходного $x$, тремя числами: наихудшей, средней и относительной ошибкой.

## Формулы

$$e = x - \hat x, \qquad \max\lvert e\rvert = \max_n \lvert e[n]\rvert, \qquad \mathrm{RMS} = \sqrt{\overline{e^2}} = \sqrt{\frac{1}{N}\sum_n e[n]^2}, \qquad \frac{\|e\|_2}{\|x\|_2}$$

Допуск на точное восстановление при $J$ уровнях и фильтре длины $L$ (§13.2), $u = 2^{-53}$ для double:

$$\varepsilon = 100\, J\, L\, u\,\|x\|_\infty$$

## Алгоритм и стоимость

1. Длины $x$ и $\hat x$ сравниваются; при несовпадении ошибка бесконечна.
2. $\max\lvert e\rvert$ — один проход с запоминанием наибольшего $\lvert e[n]\rvert$; NaN в разности возвращается как NaN, чтобы проверка не прошла.
3. $\mathrm{RMS}$ и относительная ошибка — суммы квадратов с компенсацией Неймайера (§13.2), затем корень.

Стоимость $O(N)$, память $O(1)$.

## С числами этого блока

Наихудшая ошибка $\max\lvert e\rvert = {{metric.max_abs}}$, среднеквадратичная $\mathrm{RMS} = {{metric.rms}}$, относительная $\|e\|_2 / \|x\|_2 = {{metric.relative}}$.

## Проверки

- Точное восстановление на этом сигнале, $\max\lvert \hat x - x\rvert \le \varepsilon$: {{check.round_trip}}
- NaN в одном из сигналов никогда не проходит проверку.

## Связи

- §14.1 — определения $\max\lvert e\rvert$, RMS и относительной ошибки.
- §13.2 — рост ошибки по уровням и допуск тестов.
- §14.2 — SNR и PSNR: те же суммы квадратов в децибелах.
- §11.1 — DWT и обратное преобразование, чьё восстановление проверяется.
- §1.1 — ошибка FFT-свёртки абсолютна, поэтому сравнивать с $\|x\|$, а не поэлементно.

## Реализация

Наихудшая и среднеквадратичная ошибка двух сигналов.

```cpp
double max_abs_error(std::span<const double> a, std::span<const double> b) {
    if (a.size() != b.size()) return std::numeric_limits<double>::infinity();
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double d = std::abs(a[i] - b[i]);
        if (std::isnan(d)) return d;
        if (d > worst) worst = d;
    }
    return worst;
}
double rms_error(std::span<const double> a, std::span<const double> b) {
    if (a.size() != b.size()) return std::numeric_limits<double>::infinity();
    if (a.empty()) return 0.0;
    return std::sqrt(squared_difference_sum(a, b) / static_cast<double>(a.size()));
}
```
