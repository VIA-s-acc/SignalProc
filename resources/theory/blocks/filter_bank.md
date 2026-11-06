+++
id = "filter_bank"
title = "Двухканальный банк фильтров"
theory = ["two-channel", "fb-distortion-alias", "fb-alias-cancellation", "fb-cqf", "conv-pywt"]
source = "src/core/bank.cpp"
function = "check_pr"
test = "tests/core/test_bank.cpp"
views = ["designer", "filters"]
+++

## Назначение

Описать банк $(h_0, h_1, f_0, f_1)$: анализ, прореживание $\downarrow 2$, вставку нулей $\uparrow 2$ и синтез — и проверить, что на выходе получается исходный сигнал с чистой задержкой $\ell$, то есть без наложения и искажения.

## Формулы

Канал анализа $v_i = {\downarrow}2\,(h_i * x)$, синтез $\hat x = \sum_i f_i * {\uparrow}2\, v_i$. Тогда

$$\hat X(z) = T(z)\, X(z) + A(z)\, X(-z), \qquad T = \tfrac12\left[F_0 H_0 + F_1 H_1\right], \qquad A = \tfrac12\left[F_0(z) H_0(-z) + F_1(z) H_1(-z)\right]$$

PR с задержкой $\ell$: $A \equiv 0$ и $T = z^{-\ell}$. Стандартный выбор ФВЧ (Strang–Nguyen, [SN]) гасит наложение при любых $H_0, F_0$:

$$H_1(z) = F_0(-z), \qquad F_1(z) = -H_0(-z) \quad \Rightarrow \quad A \equiv 0, \qquad T = \tfrac12\left[P_0(z) - P_0(-z)\right], \qquad P_0 = F_0 H_0$$

Поэтому PR $\iff P_0(z) - P_0(-z) = 2 z^{-\ell}$: у $P_0$ единственный нечётный коэффициент $p_0[\ell] = 1$. В массивах от нуля это $h_1[n] = (-1)^n f_0[n]$ и $f_1[n] = (-1)^{n+1} h_0[n]$ (§0.4).

Ортогональный банк (CQF, длина $L$ чётна):

$$f_0[n] = h_0[L-1-n], \qquad \sum_n h_0[n]\, h_0[n+2k] = \delta[k]$$

## Алгоритм и стоимость

1. Многочлены Лорана $T$ и $A$ вычисляются точным произведением коэффициентов $F_0 H_0$, $F_1 H_1$ и их аналогов с $H_i(-z)$.
2. Наложение — наибольший по модулю коэффициент $A$.
3. Искажение: положение наибольшего $|T|$ даёт $\ell$; мера — наибольшее отклонение коэффициентов $T$ от единичного импульса в позиции $\ell$.
4. PR засчитан, если обе меры не превышают допуск; тогда определена и задержка $\ell$.

Стоимость — четыре произведения фильтров по $O(L^2)$, от длины сигнала не зависит. Сам банк на сигнале стоит $\approx N\,(L_0 + L_1)$ умножений на уровень.

## С числами этого блока

Банк «{{block.bank}}»: длины фильтров — {{block.length}}, линейная фаза — {{block.linear_phase}}. Задержка PR $\ell = {{block.delay}}$ отсчётов: на выходе $\hat x[n] = x[n - \ell]$.

В раскладке pywt все четыре фильтра имеют одну чётную длину $L$, а $P_0 = F_0 H_0$ центрирован в $L - 1$; поэтому $\ell = L - 1$ (Хаар: $\ell = 1$, 5/3 при $L = 6$: $\ell = 5$).

## Проверки

- Наложение $A \equiv 0$, наибольший модуль коэффициентов $A$: {{check.pr_alias}}
- Искажение $T = z^{-\ell}$, наибольшее отклонение коэффициентов $T$ от импульса в позиции $\ell$: {{check.pr_distortion}}
- Ортогональность $\sum_n h_0[n]\, h_0[n+2k] = \delta[k]$ и $f_0$ — обращённый $h_0$: {{check.orthogonality}}. У биортогональных банков (5/3, 9/7) эта проверка не проходит по построению: линейная фаза и ортогональность несовместимы, кроме Хаара.

## Связи

- §4.1 — уравнения искажения и наложения.
- §4.2 — как выбрать синтез, чтобы наложение гасилось само.
- §4.4 — CQF: ортогональный банк как частный случай.
- §4.5 — полифазная запись PR и параунитарность.
- §5.2 — пример на банке Хаара: анализ, $\downarrow 2$, $\uparrow 2$, синтез по шагам.
- §0.4 — знаки ФВЧ и соответствие PyWavelets.

## Реализация

Проверка PR: положение пика $T$ даёт задержку $\ell$, мера искажения — отклонение остальных коэффициентов от нуля и пика от единицы; наложение и $T$ перед этим собраны как произведения фильтров.

```cpp
    if (distortion.coeffs.empty()) {
        report.distortion.value = 1.0;
        return report;
    }
    const auto peak = std::ranges::max_element(distortion.coeffs, {}, [](double c) { return std::abs(c); });
    const auto ell = distortion.offset + (peak - distortion.coeffs.begin());
    double deviation = 0.0;
    for (std::size_t k = 0; k < distortion.coeffs.size(); ++k) {
        const auto n = distortion.offset + static_cast<std::ptrdiff_t>(k);
        deviation = nan_max(deviation, std::abs(distortion.coeffs[k] - (n == ell ? 1.0 : 0.0)));
    }
    report.distortion.value = deviation;
    if (report.distortion.passed() && report.alias.passed()) report.delay = ell;
```
