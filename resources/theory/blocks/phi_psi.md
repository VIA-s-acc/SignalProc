+++
id = "phi_psi"
title = "Спектры φ и ψ"
theory = ["freq-phi-psi-spectra", "freq-heisenberg"]
source = "src/core/wavelet/spectra.cpp"
function = "scaling_spectrum"
test = "tests/core/test_spectra.cpp"
views = ["filters"]
+++

## Назначение

Увидеть масштабирующую функцию и вейвлет в частотной области: $\lvert\Phi\rvert$ — ФНЧ с нулями в $2\pi k$, $\lvert\Psi\rvert$ — полосовой фильтр с пиком в октаве $[\pi, 2\pi]$. Для CWT — $\hat\Psi(a\omega)$ на нескольких масштабах: банк полосовых фильтров с постоянной добротностью.

## Формулы

$$\Phi(\omega)=\prod_{j=1}^{\infty} m_0(\omega/2^j), \qquad m_0=\frac{H(e^{i\omega})}{H(1)}$$

$$\Psi(\omega)=m_1(\omega/2)\,\Phi(\omega/2), \qquad m_1=\frac{G(e^{i\omega})}{H(1)}$$

Для $\varphi$ берутся $H=f_0$ и $G=f_1$, для двойственной $\tilde\varphi$ — обращённые $h_0$ и $h_1$.

Нуль порядка $N$ у $m_0$ в точке $\pi$ даёт нули $\Phi$ того же порядка в $2\pi k$, $k\neq0$ (условие Стрэнга–Фикса). У нуля $\lvert\Psi(\omega)\rvert\sim\lvert\omega\rvert^{\tilde N}$ — на логарифмической оси это наклон $20\tilde N$ дБ на декаду.

## Алгоритм и стоимость

1. Проверить $H(-1)=0$. Без нуля в $-1$ произведение не убывает и $\varphi\notin L^2$ — спектр не строится, причина показывается.
2. Перемножить $J$ множителей, пока $2^{-J}\lvert\omega\rvert<10^{-9}$: около 35 для $\omega\le8\pi$. Каждый множитель — значение многочлена по схеме Горнера.
3. Отброшенный хвост заменить множителем $e^{-i\tau\omega 2^{-J}}$, где $\tau$ — центр масс фильтра. Это убирает фазовую ошибку усечения.
4. Клавиши o и O меняют число множителей: $\Phi_J$ показывает сходимость каскада в частотной области.

Стоимость $O(PJL)$ для $P$ точек и фильтра длины $L$.

## С числами этого блока

Спектр: {{block.filter}}; множителей: {{block.factors}}.

Порядок нулей: {{metric.zeros}}; разбиение единицы: {{metric.partition}}.

## Проверки

- Хаар: $\Phi(\omega)=(1-e^{-i\omega})/(i\omega)$ до $10^{-12}$, $\lvert\Psi(\omega)\rvert=4\sin^2(\omega/4)/\omega$.
- db4: $\sum_k\lvert\Phi(\omega+2\pi k)\rvert^2=1$ по $\lvert k\rvert\le400$ до $10^{-6}$; db3: $\sum_j\lvert\Psi(2^j\omega)\rvert^2=1$.
- $\Phi(2\pi k)=0$ при $k\neq0$ для db2, sym5, bior2.2 и coif2.
- $\hat\Psi$ для morl, mexh и cmor совпадает с численным преобразованием $\psi$ до $10^{-9}$.

## Связи

- §8.9 — спектры φ и ψ.
- §8.10 — частотно-временное разбиение.

## Реализация

Произведение с поправкой хвоста.

```cpp
        std::complex<double> p = std::polar(1.0, -centre * std::ldexp(omega[k], -static_cast<int>(count)));
        for (std::size_t j = 1; j <= count; ++j) p *= dtft(lowpass, std::ldexp(omega[k], -static_cast<int>(j))) / gain;
```
