# Signals

Банки фильтров, вейвлеты и матрицы Тёплица на C++ — с интерактивным терминальным интерфейсом, в котором видно всё: характеристики фильтров, схему банка, каждую полосу разложения, ход обработки и теорию каждого метода.

Проект переписывает старую Python-библиотеку `SignalProc` (2023). Её код сохранён в `legacy/python/` для сравнения.

## Состояние

Идёт этап M1 — ядро банков фильтров. План: [docs/superpowers/plans/2026-11-05-signals-m0-m1.md](docs/superpowers/plans/2026-11-05-signals-m0-m1.md).

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
src/            библиотеки C++ (ядро, CLI, позже — интерфейс)
app/            исполняемый файл signals
tests/          тесты и эталоны PyWavelets (tests/golden/)
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
