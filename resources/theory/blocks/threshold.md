+++
id = "threshold"
title = "Порог — сжатие коэффициентов деталей"
theory = ["audio-denoising"]
source = "src/core/process/shrink.cpp"
function = "denoise"
test = "tests/core/test_shrink.cpp"
views = ["decomposition"]
+++

## Назначение

Считать малые коэффициенты деталей $d_j$ шумом и обнулить или сжать их, оставив крупные, которые несут сигнал: модель $y = x + \sigma e$ с белым гауссовым $e$.

## Формулы

Оценка шума по самому мелкому уровню (медиана $\lvert N(0,1)\rvert$ равна $0{,}6745$) и пороги:

$$\hat\sigma = \frac{\operatorname{median}\,\lvert d_1[k]\rvert}{0{,}6745}, \qquad \lambda_{\mathrm{Visu}} = \hat\sigma\sqrt{2\ln N}, \qquad \lambda_{\mathrm{Bayes}} = \frac{\hat\sigma^2}{\hat\sigma_X}$$

$$\hat\sigma_X = \sqrt{\max\left(\hat\sigma_Y^2 - \hat\sigma^2,\, 0\right)}, \qquad \hat\sigma_Y^2 = \frac{1}{N_j}\sum_k d_j[k]^2$$

Функции сжатия коэффициента $y$ при пороге $\lambda$ (жёсткая, мягкая, неотрицательная гаррота):

$$\eta_H(y) = y\,\mathbf 1\{\lvert y\rvert > \lambda\}, \qquad \eta_S(y) = \operatorname{sign}(y)\,(\lvert y\rvert - \lambda)_+, \qquad \eta_G(y) = \left(y - \frac{\lambda^2}{y}\right)\mathbf 1\{\lvert y\rvert > \lambda\}$$

Результат: $\hat d_j[k] = \eta\big(d_j[k];\, \lambda_j\big)$, аппроксимация $a_J$ не меняется, $\hat x = \mathrm{IDWT}\big(a_J, \hat d_J, \dots, \hat d_1\big)$.

## Алгоритм и стоимость

1. DWT на $J$ уровней (в `denoise` по умолчанию $J_{\max} - 3$, но не меньше 1).
2. $\hat\sigma$ — по $d_1$ (нулевые коэффициенты в медиане не учитываются) либо заданное вручную.
3. Для каждой детали $d_j$ порог $\lambda_j$: VisuShrink — один на все уровни, BayesShrink — по энергии своей полосы; при $\hat\sigma_X \to 0$ порог огромен и полоса подавляется целиком.
4. Функция сжатия к каждому $d_j$; при $\hat\sigma = 0$ (тишина) сигнал возвращается как есть.
5. IDWT.

Мягкая функция непрерывна, но смещает большие коэффициенты на $\lambda$; жёсткая смещает меньше, зато оставляет «музыкальный шум»; гаррота — компромисс.

Стоимость: анализ и синтез по $\approx N\,(L_0 + L_1)$ умножений, медиана и сжатие $O(N)$, память $O(N)$.

## С числами этого блока

Правило {{block.rule}}, функция сжатия {{block.mode}}, затронутые уровни: {{block.levels}}.

Оценка шума $\hat\sigma = {{metric.sigma}}$; пороги по уровням $\lambda_j$: {{block.thresholds}}.

Оставлено коэффициентов деталей: {{metric.kept}}; снято энергии деталей: {{metric.energy_removed}}.

## Проверки

Проверить стоило бы тождественность при $\lambda = 0$, оценку $\hat\sigma$ на тестовых сигналах Donoho–Johnstone (в пределах 5 %) и совпадение с pywt и scikit-image — эти проверки живут в `tests/core/test_threshold.cpp` и `test_shrink.cpp`, на экране их нет.

## Связи

- §12.1 — модель шума, SureShrink и усреднение по циклическим сдвигам.
- §11.1 — пирамида Малла, откуда берутся $d_j$.
- §11.2 — SWT: порог без привязки к фазе сдвига.
- §12.3 — обнуление полосы целиком, грубый порог и порождаемое им наложение.
- §14.2 — SNR: как измерить прирост от шумоподавления.
- [M] — оценка и пороговая обработка в вейвлетных базисах.

## Реализация

Пороги по уровням из оценки $\hat\sigma$ и затем сжатие каждой детали выбранной функцией.

```cpp
DenoiseReport report{options.sigma.value_or(noise_sigma_mad(coeffs->bands.back())), levels, {}};
for (std::size_t b = 1; b < coeffs->bands.size(); ++b) {
    const double t = options.rule == ShrinkRule::bayes ? bayes_threshold(coeffs->bands[b], report.sigma)
                                                       : universal_threshold(report.sigma, x.size());
    report.thresholds.push_back(t);
}
if (report.sigma == 0) return std::pair{std::vector<double>(x.begin(), x.end()), std::move(report)};

for (std::size_t b = 1; b < coeffs->bands.size(); ++b)
    coeffs->bands[b] = threshold(coeffs->bands[b], report.thresholds[b - 1], options.threshold);
```
