+++
id = "snr"
title = "SNR и PSNR"
theory = ["met-snr"]
source = "src/core/metrics.cpp"
function = "snr_db"
test = "tests/core/test_metrics.cpp"
views = ["decomposition"]
+++

## Назначение

Выразить ошибку $e = x - \hat x$ в децибелах относительно сигнала (SNR) или относительно полной шкалы (PSNR).

## Формулы

$$\mathrm{SNR} = 10\lg\frac{\sum_n x[n]^2}{\sum_n e[n]^2}, \qquad \mathrm{PSNR} = 10\lg\frac{p^2}{\mathrm{MSE}}, \qquad \mathrm{MSE} = \frac{1}{N}\sum_n e[n]^2$$

Здесь $p$ — пик шкалы: $1$ для вещественного сигнала полной шкалы, $32767$ для int16; его указывают явно. Эквивалентная запись $\mathrm{SNR} = -20\lg\big(\|e\|_2 / \|x\|_2\big)$; прибавка $20\lg 2 \approx 6{,}02$ дБ означает вдвое меньшую амплитуду ошибки. Для шумоподавления смотрят прирост $\Delta\mathrm{SNR} = \mathrm{SNR}_{\text{после}} - \mathrm{SNR}_{\text{до}}$.

## Алгоритм и стоимость

1. Длины сравниваются; при несовпадении результат $-\infty$.
2. $\sum e^2$ — сумма квадратов разности с компенсацией Неймайера, $\sum x^2$ — энергия опорного сигнала.
3. Логарифм отношения; при нулевой ошибке (сигналы совпали) результат $+\infty$.

Стоимость $O(N)$, память $O(1)$.

## С числами этого блока

Опорный сигнал — исходный $x$, проверяемый — $\hat x$.

$\mathrm{SNR} = {{metric.snr_db}}$ относительно сигнала.

$\mathrm{PSNR} = {{metric.psnr_db}}$ относительно пика шкалы.

## Проверки

Проверить стоило бы, что SNR тождественных сигналов равен $+\infty$ и что PSNR совпадает с формулой на известных векторах — эти проверки живут в `tests/core/test_metrics.cpp` и `tests/core/test_signal_ops.cpp`, на экране их нет.

## Связи

- §14.2 — SNR, PSNR и SegSNR (последняя в блок не входит).
- §14.1 — ошибка восстановления: те же суммы квадратов без логарифма.
- §12.1 — шумоподавление, для которого меряют прирост SNR.
- §13.2 — компенсированное суммирование.

## Реализация

Отношение энергии опорного сигнала к энергии разности и PSNR через среднеквадратичную ошибку.

```cpp
double snr_db(std::span<const double> reference, std::span<const double> test) {
    if (reference.size() != test.size()) return -std::numeric_limits<double>::infinity();
    const double noise = squared_difference_sum(reference, test);
    if (noise == 0.0) return std::numeric_limits<double>::infinity();
    return 10.0 * std::log10(energy(reference) / noise);
}
double psnr_db(std::span<const double> reference, std::span<const double> test, double peak) {
    if (reference.size() != test.size() || reference.empty()) return -std::numeric_limits<double>::infinity();
    const double mse = squared_difference_sum(reference, test) / static_cast<double>(reference.size());
    if (mse == 0.0) return std::numeric_limits<double>::infinity();
    return 10.0 * std::log10(peak * peak / mse);
}
```
