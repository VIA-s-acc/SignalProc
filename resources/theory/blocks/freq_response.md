+++
id = "freq_response"
title = "Частотная характеристика фильтра"
theory = ["freq-magnitude", "freq-group-delay", "freq-band-metrics", "lti-frequency-response"]
source = "src/core/freq.cpp"
function = "frequency_response"
test = "tests/core/test_freq.cpp"
views = ["filters"]
+++

## Назначение

Показать, как фильтр $h$ пропускает синусоиды разных частот: АЧХ и ЛАЧХ, фазу, групповую задержку и числа полосы (срез $-3$ дБ, пульсации, ослабление, переходная полоса).

## Формулы

$$H(e^{i\omega}) = \sum_n h[n]\, e^{-i\omega n}, \qquad \lvert H(e^{i\omega}) \rvert, \qquad 20 \log_{10} \lvert H(e^{i\omega}) \rvert \ \text{дБ}$$

$$\lvert H(1) \rvert = \Big\lvert \sum_n h[n] \Big\rvert, \qquad \lvert H(-1) \rvert = \Big\lvert \sum_n (-1)^n h[n] \Big\rvert$$

Здесь $H(1)$ — значение при $\omega = 0$, $H(-1)$ — при $\omega = \pi$.

Групповая задержка без развёртки фазы:

$$\tau_g(\omega) = \operatorname{Re} \frac{H_n(e^{i\omega})}{H(e^{i\omega})}, \qquad H_n(e^{i\omega}) = \sum_n n\, h[n]\, e^{-i\omega n}$$

Метрики полосы относительно максимума АЧХ: $\omega_c$ — первое пересечение уровня $-10 \log_{10} 2 \approx -3{,}01$ дБ; пульсации — размах ЛАЧХ в полосе пропускания; ослабление $A_s = -\max_{\text{стоп}} \lvert H \rvert_{\text{дБ}}$; переходная полоса $\Delta\omega = \omega_{-20} - \omega_{-1}$, расстояние между пересечениями уровней $-1$ и $-20$ дБ.

## Алгоритм и стоимость

1. Равномерная сетка $\omega_k = \pi k / K$, $k = 0, \dots, K-1$, на $[0, \pi)$.
2. В каждой точке один проход по отводам накапливает сразу $H$ и $H_n$: сумма по отводам с учётом смещения Лорана, без FFT.
3. $\lvert H \rvert$, ЛАЧХ с полом $-300$ дБ, $\arg H$ и развёрнутая фаза; $\tau_g = \operatorname{Re}(H_n / H)$ там, где $\lvert H \rvert > 10^{-12} \max \lvert H \rvert$, иначе не определена.
4. Метрики полосы: пересечения уровней находятся линейной интерполяцией между соседними точками сетки.

Стоимость $O(K L)$ комплексных слагаемых; память $O(K)$ на каждую кривую. Для ФНЧ полоса пропускания — до половины номинальной границы полосы, задерживания — от её полуторакратного значения (не выше $\pi$); у ФВЧ и полосовых пульсации и ослабление не определяются.

## С числами этого блока

Фильтр {{block.filter}}: $L = {{block.length}}$ отводов, сетка $K = {{block.points}}$ точек, то есть $K \cdot L = {{block.points}} \cdot {{block.length}}$ слагаемых. Линейная фаза — {{block.linear_phase}}. Кратность нуля в $z = -1$ — {{block.zeros_at_minus_one}}: вблизи $\pi$ АЧХ ведёт себя как $(\pi - \omega)^N$.

Срез $\omega_c = {{metric.cutoff_3db}}$; пульсации в полосе пропускания {{metric.ripple_db}}; ослабление в полосе задерживания {{metric.attenuation_db}}; переходная полоса $\Delta\omega = {{metric.transition}}$.

## Проверки

- Совпадение с эталоном `scipy.signal.freqz` и `group_delay`; $\lvert H(1) \rvert = \lvert \sum h \rvert$ и $\lvert H(-1) \rvert = \lvert \sum (-1)^n h \rvert$ (§8.2).
- У любого ортогонального ФНЧ срез $-3$ дБ ровно на $0{,}5\pi$ (§8.7): сравните с $\omega_c = {{metric.cutoff_3db}}$.
- У вейвлетного ФНЧ $\lvert H(1) \rvert = \sqrt2$, то есть $+3{,}01$ дБ над единицей.
- Симметричный фильтр длины $L$ имеет $\tau_g \equiv (L-1)/2$ вне нулей АЧХ; линейная фаза здесь: {{block.linear_phase}}.

## Связи

- §1.4 — линейная фаза, типы I–IV и вынужденные нули.
- §8.3 — свёрнутая и развёрнутая фаза, ловушки скачков на $\pi$.
- §8.4 — групповая задержка и фазовая задержка.
- §8.6 — карта нулей: показывать известную кратность в $-1$, а не найденные корни.
- §8.7 — метрики полос и степенная симметрия.

## Реализация

Точка сетки: сумма $H$ и взвешенная сумма $H_n$ за один проход по отводам.

```cpp
    for (std::size_t k = 0; k < points; ++k) {
        const double w = std::numbers::pi * static_cast<double>(k) / static_cast<double>(points);
        r.omega[k] = w;
        std::complex<double> sum{0, 0}, moment{0, 0};
        for (std::size_t j = 0; j < h.coeffs.size(); ++j) {
            const auto n = static_cast<double>(h.offset + static_cast<std::ptrdiff_t>(j));
            const auto term = h.coeffs[j] * std::polar(1.0, -w * n);
            sum += term;
            moment += n * term;
        }
        r.h[k] = sum;
        weighted[k] = moment;
    }
```
