# Signals

Банки фильтров, вейвлеты и матрицы Тёплица на C++ — с интерактивным терминальным интерфейсом, в котором видно всё: характеристики фильтров, схему банка, каждую полосу разложения, ход обработки и теорию каждого метода.

Проект переписывает старую Python-библиотеку `SignalProc` (2023). Её код сохранён в `legacy/python/` для сравнения.

## Состояние

Готовы M0–M1 (каркас и ядро банков фильтров) и M2 (звук, обработка, характеристики, CLI). Следующий этап — M3, каркас TUI. План M2: [docs/superpowers/plans/2026-11-05-signals-m2.md](docs/superpowers/plans/2026-11-05-signals-m2.md).

## Команды

Все сообщения — на русском. Ключ `--json` есть у команд, которые печатают отчёт; `--help` у каждой команды перечисляет её параметры.

| Команда | Что делает |
|---|---|
| `signals check` | самопроверка ядра: восстановление, ортогональность, Парсеваль, дополнительность мощности, матрицы |
| `signals filter info <имя>` | коэффициенты h₀, h₁, f₀, f₁, задержка, нули в −1, гладкость |
| `signals filter response <имя>` | АЧХ, ФЧХ, групповая задержка и метрики фильтра или эквивалентного фильтра полосы |
| `signals design --p N` | все разбиения корней полуполосного P(z) и их классификация |
| `signals info <файл.wav>` | формат и статистика каналов: пик, RMS, DC, пик-фактор, отсчёты у края |
| `signals gen <вид>` | тестовый сигнал: sine, sweep, impulse, step, noise, harmonics, am, fm, dj |
| `signals resample <файл.wav>` | сжатие ↓M или растяжение ↑M с фильтром Кайзера |
| `signals dwt <файл.wav>` | многоуровневое DWT: коэффициенты в JSON, полосы — в WAV с частотой fs/2^j |
| `signals denoise <файл.wav>` | шумоподавление BayesShrink или VisuShrink, как в scikit-image |

Примеры:

```bash
signals info resources/samples/test.wav
signals gen sweep --f0 20 --f1 20000 --log --rate 48000 --duration 2 -o sweep.wav
signals resample sweep.wav --down 2 -o sweep_24k.wav
signals dwt resources/samples/test.wav --wavelet db2 --levels 3 --channel 0 -o coeffs.json --bands-dir bands
signals denoise noisy.wav -o clean.wav --wavelet sym8 --rule bayes --reference reference.wav
signals filter response db2 --band d --level 2 --points 1024 --json
```

WAV читается во всех распространённых форматах: 8, 16, 24 и 32 бита, float32 и float64, A-law и μ-law, `WAVE_FORMAT_EXTENSIBLE`. Пишется 24 бита с TPDF-дизерингом. Значения вне диапазона ограничиваются и считаются, а не заворачиваются. Каналы обрабатываются по отдельности.

## Документы

| Файл | Что в нём |
|---|---|
| [docs/03-design.md](docs/03-design.md) | проектный документ: цели, архитектура, функции, интерфейс, этапы |
| [docs/02-theory.md](docs/02-theory.md) | теория: банки фильтров, вейвлеты, матрицы Тёплица, частотные характеристики |
| [docs/01-review-signalproc.md](docs/01-review-signalproc.md) | ревизия старого Python-кода |
| [docs/research/](docs/research/) | исследования стека, функций и визуализации |
| [docs/design/](docs/design/) | исходники экранов дизайна TUI |

## Раскладка

```
src/            библиотеки C++: core (ядро), io (WAV, генераторы), commands (CLI), позже — интерфейс
app/            исполняемый файл signals
tests/          тесты, эталоны PyWavelets, scipy и scikit-image (tests/golden/), WAV-фикстуры (tests/fixtures/)
tools/          генератор эталонов на Python
resources/      словарь интерфейса, пример test.wav
legacy/python/  старый SignalProc
docs/           документация
```

## Сборка

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Нужны CMake ≥ 3.28 и компилятор с C++23: Apple Clang 17 или GCC 14.

## Лицензия

MIT, см. [LICENSE](LICENSE).
