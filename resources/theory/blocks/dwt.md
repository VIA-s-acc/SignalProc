+++
id = "dwt"
title = "DWT — пирамида Малла"
theory = ["dwt-mallat", "conv-pywt", "fbm-periodization"]
source = "src/core/dwt.hpp"
function = "dwt_level"
test = "tests/core/test_dwt.cpp"
views = ["decomposition", "matrix", "filters"]
+++

## Назначение

Разложить сигнал $x$ на октавные полосы $d_1, \dots, d_J$ и $a_J$ без потери информации.

## Формулы

$$a_j[k] = \sum_n h_0[n]\, a_{j-1}[2k+1-n], \qquad d_j[k] = \sum_n h_1[n]\, a_{j-1}[2k+1-n]$$

$$a_{j-1}[m] = \sum_k \left(f_0[m-2k]\, a_j[k] + f_1[m-2k]\, d_j[k]\right), \qquad a_0 = x$$

Фаза pywt: после свёртки берутся нечётные отсчёты (§0.4) — поэтому «$2k+1$».

## Алгоритм и стоимость

1. $a_0 \leftarrow x$.
2. Для $j = 1, \dots, J$: $a_j = {\downarrow}2\,(h_0 * a_{j-1})$ и $d_j = {\downarrow}2\,(h_1 * a_{j-1})$.
3. Края — режим продолжения сигнала (§6.4).

Стоимость $\approx N\,(L_0 + L_1)$ умножений, память $O(N)$, уровней не больше $\lfloor \log_2 (N/(L-1)) \rfloor$.

## С числами этого блока

$N = {{block.n}}$, фильтр {{block.wavelet}}: $L_0 = L_1 = {{block.filter_length}}$ ⇒ ≈ {{metric.multiplies}} умножений на канал; $J = {{block.levels}}$ из $J_{\max} = {{block.max_level}}$, края — {{block.mode}}.

Длины полос: {{block.band_lengths}}; сумма {{block.band_sum}}.

Полосы частот: {{block.band_ranges}}.

## Проверки

- Точное восстановление на этом сигнале, $\max|\hat x - x|$: {{check.round_trip}}
- Ортонормальность $\sum_n h_0[n]\, h_0[n-2k] = \delta[k]$: {{check.orthogonality}}
- Нулевые моменты $\sum_n n^p h_1[n] = 0$: {{check.vanishing_moments}}
- Парсеваль, энергия полос равна энергии сигнала: {{check.parseval}}
- Линейная фаза: {{block.linear_phase}}
- Нет инвариантности к сдвигу: сдвиг $x$ на один отсчёт перераспределяет энергию по уровням, отсюда SWT (§11.2).

## Связи

- §4.1 — уравнения искажения и наложения, откуда берётся точное восстановление.
- §7.3 — каскадный алгоритм: к каким $\varphi$ и $\psi$ сходится пирамида.
- §11.2 — SWT, вариант без прореживания.
- §6.4 — периодизация и блочный циркулянт.

## Реализация

Один уровень анализа: свёртка с $h_0$ и $h_1$ и прореживание с фазой pywt.

```cpp
for (std::size_t i = 0; i < len; ++i) {
    if (stop_requested(progress, i)) return std::unexpected{cancelled_error("dwt")};
    const auto top = 2 * static_cast<std::ptrdiff_t>(i) + 1;
    const bool interior = top - (filter_len - 1) >= 0 && top < n;
    T a(0), d(0);
    for (std::ptrdiff_t k = 0; k < filter_len; ++k) {
        const T s = interior ? x[static_cast<std::size_t>(top - k)] : extended_sample(x, top - k, options.mode);
        a += lo[static_cast<std::size_t>(k)] * s;
        d += hi[static_cast<std::size_t>(k)] * s;
    }
    out.approx[i] = a;
    out.detail[i] = d;
}
```
