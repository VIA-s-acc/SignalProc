+++
id = "smoothing"
title = "Сглаживание: медиана и Савицкий–Голей"
theory = ["cls-least-squares", "fbm-boundary-modes", "lti-convolution"]
source = "src/pipeline/blocks_design.cpp"
function = "Savgol"
test = "tests/pipeline/test_blocks_analysis.cpp"
views = ["under_the_hood"]
+++

## Назначение

Убрать шум, не размывая форму сигнала: `median` заменяет отсчёт медианой окна и снимает одиночные выбросы, `savgol` (Савицкий–Голей) подгоняет в окне многочлен по МНК и берёт его значение или производную в центре.

## Формулы

`median` с нечётным окном $K = 2h+1$ и нулями за краями, как `scipy.signal.medfilt`:

$$y[i] = \mathrm{median}\big(x[i-h], \dots, x[i+h]\big), \qquad x[n] = 0 \text{ вне } [0, N)$$

`savgol` с нечётным окном $W = 2h+1$, порядком $p < W$ и производной $d \le p$. Координата в окне нормирована, $u_k = (k-h)/s$, $s = \max(1, h)$, — так матрица Вандермонда остаётся обусловленной до $p = 20$:

$$\hat\beta = \arg\min_{\beta} \sum_{k=0}^{W-1} \Big(x[i-h+k] - \sum_{j=0}^{p} \beta_j u_k^j\Big)^2, \qquad y[i] = d!\,\hat\beta_d \Big(\frac{f_s}{s}\Big)^d$$

Так как задача линейна, это свёртка с постоянным ядром: $V = QR$, $c = Q\,R^{-T}\ell$, $\ell = d!\,e_d$, и $y[i] = (f_s/s)^d \sum_k c_k\,x[i-h+k]$. Множитель $(f_s/s)^d$ — то же, что `delta = 1/fs` в `scipy.signal.savgol_filter`; при $d = 0$ частота не нужна.

Края `savgol` (`mode`): `interp` — для первых и последних $h$ отсчётов многочлен подгоняется к крайним $W$ отсчётам и вычисляется в их точках; `mirror` — $x[-m] = x[m]$ без повтора крайнего отсчёта; `nearest` — крайнее значение; `constant` — нули; `wrap` — периодически.

## Алгоритм и стоимость

1. `median`: сигнал дополняется $h$ нулями с каждой стороны; отсортированное окно обновляется на каждом шаге — уходящий отсчёт удаляется, приходящий вставляется двоичным поиском.
2. `savgol`: матрица $V_{kj} = u_k^j$ раскладывается QR-методом Хаусхолдера один раз; из $R$ и $Q$ получается ядро $c$.
3. Сигнал продолжается по `mode` и сворачивается с ядром; в режиме `interp` края пересчитываются тем же QR на крайних отрезках (нужно $N \ge W$).

Стоимость `median` — $O(N\,K)$ из-за сдвигов в окне; `savgol` — $O(N\,W)$ на свёртку (для длинных окон через БПФ) плюс $O(W\,p^2)$ на QR.

## С числами этого блока

`median`: окно $K$ = {{block.kernel}}.

`savgol`: окно $W$ = {{block.window}}, порядок $p$ = {{block.polyorder}}, производная $d$ = {{block.deriv}}, края — {{block.mode}}.

Время узла {{block.wall}}, память {{block.bytes}}.

## Проверки

- `median` для окон 5 и 11 совпадает со `scipy.signal.medfilt` точно (разность 0); чётное окно 4 отклоняется.
- `savgol` совпадает со `scipy.signal.savgol_filter` (с `delta = 1/fs`) в семи наборах — все пять режимов краёв, $d$ от 0 до 3, $p$ до 5, окно до 31 — до $10^{-10}$ от наибольшего $\lvert y\rvert$.
- Сигнал из 7 отсчётов при окне 11 в режимах `mirror`, `nearest`, `constant`, `wrap` совпадает со scipy до $10^{-12}$; в `interp` сигнал короче окна (3 отсчёта) отклоняется.
- Производная прямой с наклоном 0,5 в секунду при $f_s = 1000$ равна 0,5 до $10^{-10}$ во всех точках, включая края; чётное окно, $p \ge W$, $d > p$ и производная без частоты дискретизации дают ошибку.

## Связи

- §9.5 — метод наименьших квадратов.
- §6.5 — режимы границ и длины выходов.
- §1.1 — свёртка.
- Карточка «Рекурсивные фильтры» — сглаживание БИХ-фильтром через `lfilter` или `biquad`.

## Реализация

Ядро Савицкого–Голея из QR-разложения: решение $R^{T} z = \ell$ и умножение на $Q$.

```cpp
        const auto m = static_cast<Eigen::Index>(order + 1);
        const Eigen::MatrixXd r = qr_.matrixQR().topLeftCorner(m, m).triangularView<Eigen::Upper>();
        Eigen::VectorXd z = r.transpose().triangularView<Eigen::Lower>().solve(derivative_row(0.0));
        Eigen::VectorXd padded = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(window));
        padded.head(m) = z;
        const Eigen::VectorXd h = qr_.householderQ() * padded;
```
