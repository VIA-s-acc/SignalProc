+++
id = "envelope_window"
title = "Окна и фейды"
theory = ["cls-window-method", "stft"]
source = "src/pipeline/blocks_level.cpp"
function = "tukey"
test = "tests/pipeline/test_blocks_more.cpp"
views = ["signal"]
+++

## Назначение

`window` умножает каждый канал целиком на окно его длины — Ханна, Хэмминга, Блэкмана, Кайзера или Тьюки; `fade` плавно поднимает уровень от нуля в начале и опускает до нуля в конце.

## Формулы

Окна симметричные, как `scipy.signal.get_window(..., fftbins=False)`: знаменатель $N - 1$, $\theta_n = 2\pi n/(N-1)$:

$$w_{\mathrm{hann}} = \tfrac12 - \tfrac12\cos\theta_n, \qquad w_{\mathrm{hamming}} = 0{,}54 - 0{,}46\cos\theta_n, \qquad w_{\mathrm{blackman}} = 0{,}42 - 0{,}5\cos\theta_n + 0{,}08\cos 2\theta_n$$

$$w_{\mathrm{kaiser}}[n] = \frac{I_0\big(\beta\sqrt{1 - r_n^2}\big)}{I_0(\beta)}, \qquad r_n = \frac{2n}{N-1} - 1$$

Тьюки с долей $\alpha$: косинусные склоны на $m = \lfloor \alpha(N-1)/2 \rfloor$ отсчётов с каждой стороны, середина равна 1:

$$w[n] = \tfrac12\big(1 + \cos\pi(\tfrac{2n}{\alpha(N-1)} - 1)\big), \quad n \le m; \qquad w[n] = \tfrac12\big(1 + \cos\pi(\tfrac{2n}{\alpha(N-1)} - \tfrac{2}{\alpha} + 1)\big), \quad n \ge N - m - 1$$

$\alpha \le 0$ — прямоугольное окно, $\alpha \ge 1$ — Ханн; при $N < 2$ окно равно 1.

Умножение во времени — круговая свёртка спектров: $Y[k] = \frac1N\sum_j X[j]\,W[(k - j) \bmod N]$. Спектр сигнала размывается главным лепестком окна, а боковые лепестки задают утечку.

Фейд длины $L$ в начале и $L'$ в конце с формой $\varphi$, для $i < L$ и $i < L'$ соответственно:

$$y[i] = \varphi(i/L)\,x[i], \qquad y[N-1-i] = \varphi(i/L')\,x[N-1-i], \qquad \varphi(t) = t \quad \text{или} \quad \varphi(t) = \tfrac12 - \tfrac12\cos\pi t$$

Первый и последний отсчёты обнуляются, последний изменённый получает $\varphi((L-1)/L) < 1$, а не 1; если $L + L' > N$, множители перемножаются. Косинусная форма — это $\sin^2(\pi t/2)$: подъём и спад в сумме дают 1.

## Алгоритм и стоимость

1. Окно строится заново для каждого канала по его длине (`symmetric_window` в ядре; Тьюки — в самом блоке).
2. Поэлементное умножение.
3. `fade`: длины в отсчётах или секундах переводятся в отсчёты; два прохода — по началу и по концу.

Стоимость $O(N)$ на канал; у Кайзера две функции Бесселя $I_0$ на отсчёт. Память — окно и выход.

## С числами этого блока

Окно {{block.kind}}; $\beta$ Кайзера {{block.beta}}, доля склонов Тьюки {{block.alpha}}.

Фейд: подъём {{block.fade_in}}, спад {{block.fade_out}}, форма {{block.curve}}.

Время узла {{block.wall}}, память {{block.bytes}}.

## Проверки

- Все окна (Ханн, Хэмминг, Блэкман, Кайзер с $\beta = 8{,}6$ и $3$, Тьюки с $\alpha = 0{,}5$ и $0{,}2$) на 37 единицах совпадают со `scipy.signal.get_window(..., fftbins=False)` до $10^{-13}$; одиночный отсчёт не меняется.
- `fade` на шести единицах с подъёмом 4 и спадом 2 даёт точно (0; 0,25; 0,5; 0,75; 0,5; 0); косинусный подъём длины 2 даёт 0 и 0,5 до $10^{-15}$.

## Связи

- §9.3 — метод окон: ширина главного лепестка, уровень боковых и формула Кайзера.
- §11.9 — STFT: там окно Ханна периодическое (знаменатель $N$), здесь симметричное.
- Карточка «Спектрограмма STFT» — то же окно, но на каждом кадре.

## Реализация

Склоны окна Тьюки — та же запись, что у scipy.

```cpp
    const double last = static_cast<double>(n - 1);
    const auto width = static_cast<std::size_t>(std::floor(alpha * last / 2.0));
    for (std::size_t k = 0; k <= width; ++k)
        w[k] = 0.5 * (1 + std::cos(std::numbers::pi * (-1 + 2.0 * static_cast<double>(k) / alpha / last)));
    for (std::size_t k = n - width - 1; k < n; ++k)
        w[k] = 0.5 * (1 + std::cos(std::numbers::pi * (-2.0 / alpha + 1 + 2.0 * static_cast<double>(k) / alpha / last)));
```
