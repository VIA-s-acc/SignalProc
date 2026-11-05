# C++ стек для переписывания wavelet / filter-bank toolkit

Дата исследования: 2026-11-05. Версии и даты взяты через `gh api` (GitHub Releases/Tags/commits) и сверены с заголовками/LICENSE в репозиториях. Всё, что не проверено в этой сессии, помечено **«не проверено»**.

Локальная проверка выполнена на: Apple clang 17.0.0 (clang-1700.0.13.5), Xcode 16.4, arm64, CMake 4.3.1. Xcode 26.x в сессии не было, поэтому результаты по более новому Apple Clang не проверены.

**Ограничение сессии.** Сборка скачанного внешнего кода (FTXUI, miniaudio, dr_libs, Eigen и др.) была заблокирована политикой песочницы. Поэтому:
- шаг `cmake` configure с FetchContent выполнен успешно (все теги ниже реально клонируются, 36 с, чистый кэш);
- скетчи FTXUI и miniaudio **не компилировались**; их API сверен только по заголовкам v7.0.3 и 0.11.26.

---

## 1. TUI-библиотека

| Библиотека | Версия / дата | Лицензия | Мышь | Layout | Виджеты | Рисование | Пиксельная графика | Вердикт |
|---|---|---|---|---|---|---|---|---|
| **FTXUI** | v7.0.3, 2026-08-06; push 2026-11-01; ~10.9k stars | MIT | клик / drag / колесо (`Mouse::Left/Middle/Right`, `WheelUp/Down/Left/Right`, `Pressed/Released/Moved`, модификаторы) | flexbox-подобный (`hbox/vbox/flex/size`), **`ResizableSplitLeft/Right/Top/Bottom`** с перетаскиваемым разделителем | Menu, Toggle, Slider, Checkbox, Radiobox, Dropdown, Input, Button, Collapsible, Modal, `Container::Tab`, `gauge`, `graph`, table | `Canvas` (braille 2x4, block, текст, `DrawCell`, `DrawSurface`), true colour (`Color::RGB`), `NO_COLOR` с 7.0.1 | нет | **рекомендуется** |
| notcurses | v3.0.17, 2025-10-28; push 2026-05-18; ~4.8k stars | Apache-2.0 (README) | да (`notcurses_mice_enable`) | плоскости (planes) с z-порядком, ручная геометрия, нет flexbox | menu, selector, multiselector, reader, reel, progbar, plots (`ncuplot`), tabbed | blitters, true colour | **да: Sixel и Kitty** (`NCBLIT_PIXEL`) | сильнейший по графике, но C API и больше ручного кода |
| magiblot/tvision | master push 2026-10-09; формальных релизов нет | Borland disclaimer + MIT-подобная (`COPYRIGHT`) | да (README) | оверлапящие окна, ручной `TRect` | окна, меню, диалоги, scrollbar | нет примитивов графиков | нет | ретро-стиль, не подходит |
| ImTui | v1.0.5, 2021-04-08 (push 2025-10) | MIT | да | Dear ImGui | весь ImGui | **только 256 цветов**, поверх ncurses | нет | не подходит (нет true colour) |
| cpp-terminal | в CMake 1.0.0, тег только v0.1.0 (2020) | MIT | есть `mouse.hpp` | нет | нет (низкоуровневая библиотека) | цвета, клавиатура, resize | нет | только как низкий уровень |
| TermOx / Upp::Tui | не проверено | не проверено | не проверено | не проверено | не проверено | не проверено | не проверено | найдено только поиском, активность не проверялась |

URL: https://github.com/ArthurSonzogni/FTXUI/releases, https://github.com/dankamongmen/notcurses, https://github.com/magiblot/tvision, https://github.com/ggerganov/imtui, https://github.com/jupyter-xeus/cpp-terminal.

### Рассуждение

FTXUI покрывает почти весь список требований без самописного layout: адаптивные панели, перетаскиваемые разделители мышью, gauge для прогресса, canvas для графиков.
- Из 7.0.2 в changelog: `App::PostEvent` потокобезопасен (был баг в 7.0.0, исправлен). Это ключевое для воркер-потоков.
- В v7 `ScreenInteractive` переименован в `App`, `Pixel` в `Cell`, `Image` в `Surface`; старые имена остались алиасами (changelog 7.0.0, `screen_interactive.hpp` — `using ScreenInteractive = App`).
- Есть amalgamated-версия, модули C++20, `App::TerminalCapabilities()`.

Главный компромисс: у FTXUI нет пиксельной графики (Sixel/Kitty). Для спектрограмм и heatmap-ов он даёт 2 вертикальных «пикселя» на ячейку через полублоки с RGB. Если настоящие пиксели обязательны, единственный серьёзный вариант — notcurses, но ценой ручного layout и C API.

Тепловые карты и scalogram в FTXUI удобнее всего делать собственным `Node`: писать в `Cell` символ `▀` с `foreground_color` и `background_color` (подход рассуждением, не проверялся). Pixel-режим поверх FTXUI (kitty-escape после отрисовки кадра) хрупок из-за перерисовки кадра целиком, оставить на v2.

### Скетч (FTXUI v7.0.3): воркер, прогресс, canvas, split

API сверен с заголовками тега v7.0.3, **не компилировался**.

```cpp
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

using namespace ftxui;
using namespace std::chrono_literals;

struct Stage {
  std::string name;
  std::atomic<float> progress{0.f};
  std::atomic<double> millis{0.0};
};

int main() {
  std::vector<float> wave(48000);
  for (size_t i = 0; i < wave.size(); ++i)
    wave[i] = std::sin(2 * std::numbers::pi_v<float> * 440.f * i / 48000.f);

  std::array<Stage, 3> stages{{{"decode"}, {"dwt"}, {"denoise"}}};
  auto app = App::Fullscreen();

  // jthread on Apple Clang 17 needs -fexperimental-library (verified); std::thread works without it.
  std::jthread worker([&](std::stop_token st) {
    for (auto& s : stages) {
      auto t0 = std::chrono::steady_clock::now();
      for (int k = 1; k <= 100 && !st.stop_requested(); ++k) {
        std::this_thread::sleep_for(15ms);
        s.progress = k / 100.f;
        s.millis = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - t0).count();
        app.PostEvent(Event::Custom);  // thread-safe since 7.0.2
      }
    }
  });

  Box plot_box;
  int left_width = 28;

  auto stage_pane = Renderer([&] {
    Elements rows;
    for (auto& s : stages)
      rows.push_back(hbox({text(s.name) | size(WIDTH, EQUAL, 9),
                           gauge(s.progress) | flex,
                           text(" " + std::to_string(int(s.millis.load())) + " ms")}));
    return window(text(" stages "), vbox(std::move(rows)));
  });

  auto plot_pane = Renderer([&] {
    int w = std::max(2, (plot_box.x_max - plot_box.x_min + 1) * 2);
    int h = std::max(4, (plot_box.y_max - plot_box.y_min + 1) * 4);
    Canvas c(w, h);
    auto y_of = [&](int px) {
      float v = wave[size_t(px) * wave.size() / size_t(w)];
      return int((1.f - (v * 0.5f + 0.5f)) * (h - 1));
    };
    for (int x = 0; x + 1 < w; ++x)
      c.DrawPointLine(x, y_of(x), x + 1, y_of(x + 1), Color::RGB(80, 200, 255));
    return window(text(" waveform "), canvas(std::move(c)) | flex | reflect(plot_box));
  });

  auto layout = ResizableSplitLeft(stage_pane, plot_pane, &left_width);
  layout |= CatchEvent([&](Event e) {
    if (e == Event::Character('q')) { app.Exit(); return true; }
    return false;
  });
  app.Loop(layout);
}
```

Не проверено: схождение цикла «размер canvas зависит от `plot_box`, `plot_box` зависит от размера canvas» (при `flex` должно сходиться, но не запускалось).

---

## 2. Терминалы: true colour, мышь, графика

| Терминал | Версия / дата | True colour | Мышь SGR | Kitty graphics | Sixel | iTerm2 inline images |
|---|---|---|---|---|---|---|
| iTerm2 | 3.7.7, 2026-11-03 | не проверено (общеизвестно да) | не проверено | **да** (в release notes 3.7.x правки Kitty graphics; с какой версии — не проверено) | да, с 3.3.0 | да (нативный протокол) |
| kitty | v0.50.0, 2026-11-03 | не проверено (общеизвестно да) | не проверено | да (родной протокол) | нет (по arewesixelyet) | не проверено |
| WezTerm | последний стабильный 20240203; коммиты до 2026-10-26 | не проверено | не проверено | опция `enable_kitty_graphics`, на `main` default = true (`config/src/config.rs`) | да, с 20200620 | да (docs) |
| Ghostty | v1.4.0 (тег), 2026-10-27 | не проверено (на странице features не указано) | не проверено | **да** (docs) | нет | не проверено |
| Apple Terminal.app | macOS 26 Tahoe | **да, 24-bit с Tahoe** (WWDC25 / MacRumors, вторично; в доках Apple не нашёл); до Tahoe — 256 цветов | не проверено | нет (не проверено) | нет (arewesixelyet) | не проверено |

URL: https://www.arewesixelyet.com/, https://iterm2.com/downloads.html, https://ghostty.org/docs/features, https://wezterm.org/imgcat.html, https://www.macrumors.com/2025/06/16/apples-terminal-app-macos-tahoe/.

Для tmux/screen графика проходит плохо (WezTerm docs: протокол изображений не полностью обрабатывается в мультиплексорах).

**Лестница деградации** (определять по `COLORTERM`, `TERM_PROGRAM`, ответу DA1 и запросу Kitty graphics; FTXUI даёт `App::TerminalCapabilities()`):
1. pixel (Kitty / Sixel / iTerm2) — опционально, v2;
2. true colour + braille/полублоки;
3. 256 цветов (Terminal.app до Tahoe);
4. 16 цветов, ASCII-режим.

`NO_COLOR` уважается FTXUI с 7.0.1. Формат запроса Kitty graphics для детекта по памяти, не перепроверен.

---

## 3. WAV / аудио I/O

| Библиотека | Версия / дата | Лицензия | Header-only | Форматы | Вердикт |
|---|---|---|---|---|---|
| **dr_wav** (dr_libs) | v0.14.6, 2026-10-31 (тег `wav-0.14.6`) | public domain или MIT-0 | да, один `.h` | RIFF, RIFX, RF64, W64, AIFF; u8, s12, s16, s24, s32, f32, **f64**, A-law/µ-law, MS-ADPCM, IMA-ADPCM; запись: RIFF и W64 (RF64 на запись не проверял), AIFF/RIFX без записи; `WAVE_FORMAT_EXTENSIBLE` — в коде есть | **рекомендуется** |
| libsndfile | 1.2.2, 2023-08-13 (push 2026-10-28) | LGPL-2.1 | нет (бинарная зависимость) | широкий набор, в т.ч. WAV/WAVEX/RF64 (по общим знаниям, страницу форматов не читал) | запасной вариант; LGPL — только динамическая линковка |
| AudioFile | 1.1.4, 2025-10-15 | MIT | да | WAV и AIFF; в WAV принимает PCM, IEEE float, Extensible; запись float только при `bitDepth == 32` | проще, но нет f64 и мало контроля |
| miniaudio decoders | 0.11.26, 2026-10-31 | public domain или MIT-0 | да | WAV/FLAC/MP3 декодеры, ориентирован на воспроизведение | не для точного I/O |

URL: https://github.com/mackron/dr_libs, https://github.com/libsndfile/libsndfile/releases, https://github.com/adamstark/AudioFile.

Рассуждение: dr_wav покрывает всё из требований (PCM 8/16/24/32, float32/64, multichannel, EXTENSIBLE, RF64) без бинарной зависимости и с самой свободной лицензией. Его конвертеры (`drwav_*_to_f32`) дают нормализацию. Запись 24-bit PCM и f64 проверять тестом round-trip. Не проверено: поведение `drwav` на EXTENSIBLE с нестандартной channel mask.

---

## 4. Воспроизведение

| Библиотека | Версия / дата | Лицензия | Вердикт |
|---|---|---|---|
| **miniaudio** | 0.11.26, 2026-10-31 | public domain или MIT-0 | **рекомендуется** |

URL: https://github.com/mackron/miniaudio/releases.

На macOS бэкенд CoreAudio подгружается через runtime linking (если не задан `MA_NO_RUNTIME_LINKING`; сам механизм описан в заголовке), на Linux нужны `-ldl -lpthread -lm`.

Проигрывание буфера из памяти с позицией для курсора (имена и сигнатуры проверены grep по `miniaudio.h` 0.11.26; **не компилировалось**):

```cpp
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

ma_engine engine;
ma_engine_init(nullptr, &engine);

ma_audio_buffer_config cfg =
    ma_audio_buffer_config_init(ma_format_f32, channels, frames, interleaved.data(), nullptr);
cfg.sampleRate = sample_rate;                    // field exists in 0.11.26
ma_audio_buffer buffer;
ma_audio_buffer_init(&cfg, &buffer);             // references memory; init_copy() copies it

ma_sound sound;
ma_sound_init_from_data_source(&engine, &buffer, 0, nullptr, &sound);
ma_sound_start(&sound);

ma_uint64 cursor = 0;                            // poll ~30 Hz from a UI timer thread
ma_sound_get_cursor_in_pcm_frames(&sound, &cursor);
bool done = ma_sound_at_end(&sound);
ma_sound_seek_to_pcm_frame(&sound, target);
```

Рассуждение: callback аудио-устройства идёт в своём потоке, UI опрашивает курсор по таймеру и постит `Event::Custom` в FTXUI. Буфер должен жить дольше `ma_sound`. Для проигрывания отдельных поддиапазонов нужно заранее синтезировать каждый (обратное преобразование с обнулёнными остальными) в свой буфер.

Не проверено: единицы курсора при несовпадении `sampleRate` буфера и движка (движок ресемплирует). Для офлайн-теста есть `ma_engine_config.noDevice`.

---

## 5. FFT

| Библиотека | Версия / дата | Лицензия | Real FFT | Произвольный N | SIMD | Вердикт |
|---|---|---|---|---|---|---|
| **pocketfft (C++, ветка `cpp`)** | релизов нет; ветка `cpp` @ `4d865cb`, 2026-10-18 | BSD-3 | да (r2c/c2r, DCT/DST) | да (Bluestein) | SIMD только для многомерных; многопоточность тоже для многомерных (README) | **рекомендуется по умолчанию** |
| PFFFT (форк marton78) | v1.2.0, 2026-11-03 | BSD-подобная | да | **только 2^a·3^b·5^c** (+ условие кратности, см. README) | NEON на Apple Silicon (float, double — см. README), SSE/AVX | быстрый вариант для степеней двойки |
| KissFFT | 131.2.0, 2025-10-22 | BSD-3 | да (`kiss_fftr`) | да | нет | медленнее, нужен только для простоты |
| FFTW | 3.3.11 (тег 2026-04-18) | **GPL-2.0** | да | да | да | быстрейшая, но **заставит проект быть GPL** |
| Apple Accelerate vDSP | системный | системная | да | длина f·2^n, f ∈ {1,3,5,15} (локальный SDK-заголовок `vDSP.h`, для DFT-setup n ≥ 3–4) | да | только macOS, как опциональный backend |

URL: https://github.com/mreineck/pocketfft/tree/cpp, https://github.com/marton78/pffft, https://github.com/mborgerding/kissfft, https://github.com/FFTW/fftw3.

Рекомендация: **pocketfft** как базовый и всегда доступный backend (header-only, C++11, точные twiddle-факторы — важно для «численно корректно», любой N). За интерфейсом `Fft` держать опциональные backend-ы Accelerate (macOS) и PFFFT. Для FFT-matvec Теплица нужны размеры ≥ 2N−1; можно подбирать «хорошие» размеры (в pocketfft есть `good_size`; ветки `enhanced_good_size` и `good_size_work` подтверждены, API не проверялся).

Скорость не измерялась. Решение между pocketfft, PFFFT и vDSP принимать по бенчмарку на реальных длинах (nanobench / Google Benchmark).

---

## 6. Линейная алгебра, полиномы, высокая точность

| Библиотека | Версия / дата | Лицензия | Роль | Вердикт |
|---|---|---|---|---|
| **Eigen** | 5.0.1, 2025-11-11 (GitLab); C++14 минимум | MPL-2.0 | матрицы, `unsupported/Eigen/Polynomials` | **рекомендуется** |
| Boost.Multiprecision | Boost 1.92.0, 2026-08-12 | BSL-1.0 | `cpp_bin_float_N` (header-only), `cpp_complex`, `cpp_int`, `rational_adaptor`, backends `gmp`/`mpfr`/`mpc`; в 1.92 есть `cpp_double_fp` (double-double) | **рекомендуется для высокой точности** |
| MPFR / GMP | Homebrew-заголовки `mpfr.h`, `gmp.h` присутствуют локально; версии не проверялись | LGPL-3 (GMP: LGPL-3/GPL-2) по общим знаниям | эталонные проверки, rational | опционально, динамически |
| FLINT/Arb, MPSolve | не проверено | MPSolve — GPL по памяти | корни в произвольной точности | не проверено; GPL у MPSolve исключает его |

URL: https://gitlab.com/libeigen/eigen/-/tags, https://github.com/boostorg/multiprecision, https://github.com/boostorg/boost/releases.

Факты, проверенные локально:
- **На Apple Silicon `sizeof(long double) == 8`, `__float128` отсутствует.** Квадро-точность придётся эмулировать (`cpp_bin_float_quad`, double-double) — нельзя рассчитывать на `long double` / `__float128`.
- `PolynomialSolver` в Eigen 5.0.1 использует матрицу-компаньон с балансировкой и `EigenSolver` / `ComplexEigenSolver` (QR), т.е. точность ограничена типом скаляра (читал `PolynomialSolver.h`).
- В Boost.Multiprecision есть `boost/multiprecision/eigen.hpp` (адаптер NumTraits). Компилируется ли `PolynomialSolver<cpp_bin_float_50>`, **не проверено**.

Рассуждение (не измерялось): корни полинома Добеши P(y) при большом p плохо обусловлены, double и QR по компаньону перестают давать достаточно точные корни при умеренно больших p. Практический план: считать коэффициенты и корни в `cpp_bin_float` (100–300 знаков) собственным методом Аберта–Эрлиха или Ньютона с полировкой в `cpp_complex`, затем приводить к double. `PolynomialSolver<double>` оставить как быстрый путь для малых p и как независимую проверку. Эталон: `cpp_rational` / `mpq` для точных коэффициентов.

Boost целиком тяжёл: `boost-1.92.0-cmake.tar.xz` ≈ 102 MB. Варианты: `find_package(Boost)` из Homebrew/apt, либо только репозиторий `boostorg/multiprecision` в режиме `BOOST_MP_STANDALONE` (репозиторий на теге `Boost_1_92_0` через FetchContent клонируется; **компиляция standalone-режима не проверена**).

---

## 7. CLI, JSON, форматирование

| Назначение | Кандидат | Версия / дата | Лицензия | Вердикт |
|---|---|---|---|---|
| CLI | **CLI11** | v2.7.2, 2026-08-02 | BSD-3 (LICENSE) | **рекомендуется** (C++11+, subcommands) |
| CLI | cxxopts | v3.3.1, 2025-05-26 | MIT | проще, нативных subcommands нет (по памяти) |
| CLI | argparse (p-ranav) | v3.2, 2025-01-25 | MIT | Python-подобный; не проверялось подробно |
| JSON | **nlohmann/json** | v3.12.0, 2025-04-11 | MIT | **рекомендуется для v1** |
| JSON | glaze | v9.1.0, 2026-10-28 | MIT | быстрее, но «Requires C++23»; совместимость с Apple Clang не проверена |
| Формат | **{fmt}** | 12.2.0, 2026-06-16 | MIT | переносимый вариант |
| Формат | `std::format` / `std::print` | — | — | см. таблицу в разделе 10 |

URL: https://github.com/CLIUtils/CLI11/releases, https://github.com/nlohmann/json/releases, https://github.com/stephenberry/glaze/releases, https://github.com/fmtlib/fmt/releases.

Рассуждение: в CLI-режиме объёмы JSON небольшие (метрики, тайминги, параметры), поэтому скорость парсинга не критична — выбираем nlohmann. Большие числовые массивы (матрицы, коэффициенты) отдавать бинарно / CSV, не JSON. На Apple Clang 17 `std::format` и (в режиме C++23) `std::print` компилируются и запускаются (проверено). На Linux `std::print` требует GCC 14 (cppreference, через пересказ), поэтому для переносимости на v1 взять {fmt}.

---

## 8. Тесты и бенчмарки

| Библиотека | Версия / дата | Лицензия | Заметки | Вердикт |
|---|---|---|---|---|
| **Catch2 v3** | v3.16.1, 2026-10-08 | BSL-1.0 | matchers для чисел `WithinAbs/WithinRel/WithinULP` и встроенный `BENCHMARK` (по документации, не перепроверял) | **рекомендуется** |
| doctest | v2.5.3, 2026-07-06 | MIT | быстрая компиляция, только `Approx` | лёгкая альтернатива |
| GoogleTest | v1.18.0, 2026-08-10 | BSD-3 | требует C++17 | не нужен |
| Google Benchmark | v1.10.0, 2026-10-14 | Apache-2.0 | собирается с C++17, для тестов ему нужен GoogleTest | если нужны полноценные бенчмарки |
| nanobench | v4.6.0, 2026-08-14 | MIT | один заголовок | **рекомендуется** для микро-бенчмарков |
| RapidCheck | релизов и тегов нет; master `2c3c4365`, 2026-10-05 | BSD-2 | property-based; интеграция с Catch2/doctest/gtest в `extras` (по памяти) | **рекомендуется**, пиновать по SHA |

URL: https://github.com/catchorg/Catch2/releases, https://github.com/martinus/nanobench/releases, https://github.com/emil-e/rapidcheck.

Для численного проекта RapidCheck полезен для свойств: идеальная реконструкция, ортогональность, сохранение энергии (Парсеваль), `T·x` через FFT равен плотному умножению.

---

## 9. Производительность

| Вариант | Версия / дата | Лицензия | Проверено | Вердикт |
|---|---|---|---|---|
| `std::jthread` + свой пул | — | — | **на Apple Clang 17 нужен `-fexperimental-library`** (без него `std::jthread` не найден; с флагом собирается и линкуется) | допустимо |
| `std::execution::par` | — | — | **только с `-fexperimental-library`**; без флага `std::execution::par` отсутствует. С флагом работает: бэкенд libdispatch, `transform` по 16M double: seq 59.6 ms, par 8.6 ms, par_unseq 7.9 ms на 14 ядрах (~7x) | на Linux/GCC нужен TBB — **не проверено** |
| oneTBB | v2023.2.0, 2026-10-28 | Apache-2.0 | версия по GitHub API; сборка не проверялась | надёжный, но тяжёлая зависимость |
| **Taskflow** | v4.1.0, 2026-06-20 | MIT (LICENSE) | v4 требует C++20 (README: GCC ≥ 11, Clang ≥ 12) | **рекомендуется**: header-only, граф стадий |
| xsimd | 14.3.0, 2026-07-15 (тег) | BSD-3 | C++17 | для hot-kernels |
| Google Highway | 1.4.0, 2026-04-23 | Apache-2.0 или BSD-3 | C++17 | альтернатива xsimd |
| `std::experimental::simd` | — | — | **в libc++ Apple Clang 17 отсутствует** (проверено компиляцией) | не использовать |

URL: https://github.com/taskflow/taskflow/releases, https://github.com/oneapi-src/oneTBB/releases, https://github.com/xtensor-stack/xsimd, https://github.com/google/highway/releases.

Рекомендация: Taskflow как единственный механизм параллелизма (подходит для DAG стадий «декодирование, анализ, порог, синтез» с прогрессом и таймингами). Для flat `parallel_for` внутри ядра хватает Taskflow или свой простой разбиение по блокам. SIMD сначала через автовекторизацию на простых циклах без `-ffast-math` (чтобы сохранить числовую корректность), затем xsimd на горячих ядрах (lifting-шаги, FIR).

**Реалистичная скорость свёртки / lifting не измерялась.** Ожидание (оценка, не факт): lifting и короткие FIR в double на Apple Silicon упираются в память, порядок сотен MS/s на ядро; для длинных фильтров (2p ≥ 64) переход на FFT-свёртку. Подтверждать бенчмарком.

---

## 10. Сборка и стандарт

### Версия CMake и менеджер зависимостей

| Параметр | Значение |
|---|---|
| Последний CMake | v4.4.5, 2026-10-27 (локально 4.3.1) |
| Рекомендуемый минимум | **3.28** (современный минимум; C++20-модули FTXUI требуют ≥ 3.28.2, сам FTXUI собирается и с меньшими; версии CMake в дистрибутивах не проверены) |
| FetchContent | встроен, проверен: configure со всеми зависимостями ниже прошёл за 36 с |
| CPM.cmake | v0.43.2, 2026-09-24, MIT; добавляет кэш (`CPM_SOURCE_CACHE`) |
| vcpkg | 2026.10.16, 2026-10-20; полезен для Boost / GMP / MPFR на Linux |

Рекомендация: **FetchContent** для всего header-only и небольшого, `find_package` для Boost / GMP / MPFR (Homebrew, apt). CPM.cmake — позже, если нужен общий кэш.

### Стандарт C++ (Apple Clang 17.0.0, Xcode 16.4, проверено компиляцией и запуском)

| Возможность | `-std=c++20` | `-std=c++23` |
|---|---|---|
| `std::format` | OK | OK |
| `<ranges>`, `std::span`, concepts | OK | OK |
| `std::print` / `println` | нет | OK |
| `std::expected` | нет | OK |
| `std::mdspan` (`m[1,2]`) | нет | OK |
| `std::jthread`, `stop_token` | нужен `-fexperimental-library` | нужен `-fexperimental-library` |
| `std::execution::par` | нужен `-fexperimental-library` | нужен `-fexperimental-library` |
| `std::experimental::simd` | нет | нет |

На GCC (cppreference, через пересказ, неточно): `std::expected` — 12, `std::print` — 14, `std::mdspan` — 15 или 16 (источник противоречив). Версии GCC/Clang на Linux и Xcode 26.x **не проверены**.

Рекомендация: **C++23** (`CMAKE_CXX_STANDARD 23`, расширения выключены) с {fmt} вместо `std::print` на v1. Использовать `std::span`, ranges, concepts, `std::expected`, `std::mdspan` (для матричных видов). Флаг `-fexperimental-library` добавить только если понадобится `std::jthread`; при Taskflow можно обойтись `std::thread`.

---

## 11. Лицензии

| Компонент | Лицензия | Влияние |
|---|---|---|
| FFTW | GPL-2.0 | **заставит GPL** (или нужна коммерческая лицензия) |
| MPSolve | GPL (по памяти) | заставит GPL |
| GMP / MPFR | LGPL | ок при динамической линковке; статическая усложняет |
| libsndfile | LGPL-2.1 | ок при динамической линковке |
| Eigen | MPL-2.0 | файловый copyleft, на проект не влияет |
| FTXUI, nlohmann, glaze, fmt, doctest, nanobench, Taskflow, CPM | MIT | permissive |
| notcurses, oneTBB, Google Benchmark | Apache-2.0 | permissive |
| Boost (Multiprecision), Catch2 | BSL-1.0 | permissive |
| miniaudio, dr_wav | public domain или MIT-0 | permissive |
| CLI11, pocketfft, KissFFT, xsimd, GoogleTest | BSD-3 | permissive |
| PFFFT | BSD-подобная (FFTPACK) | permissive |
| Highway | Apache-2.0 или BSD-3 | permissive |
| RapidCheck | BSD-2 | permissive |
| tvision | Borland disclaimer + MIT-подобная | permissive, нестандартный текст |

Рекомендуемый стек не содержит GPL/LGPL-зависимостей, поэтому зависимости не навязывают лицензию новому проекту. Ограничение даёт только перенос кода из старого проекта (GPL-3.0): портированный код остаётся под GPL.

---

## Рекомендуемый стек

| Область | Выбор | Версия | Лицензия |
|---|---|---|---|
| TUI | FTXUI | v7.0.3 | MIT |
| WAV | dr_wav | wav-0.14.6 | PD / MIT-0 |
| Playback | miniaudio | 0.11.26 | PD / MIT-0 |
| FFT | pocketfft (cpp) | коммит `4d865cb…` | BSD-3 |
| Линал / полиномы | Eigen | 5.0.1 | MPL-2.0 |
| Высокая точность | Boost.Multiprecision | Boost_1_92_0 | BSL-1.0 |
| CLI | CLI11 | v2.7.2 | BSD-3 |
| JSON | nlohmann/json | v3.12.0 | MIT |
| Форматирование | {fmt} | 12.2.0 | MIT |
| Тесты | Catch2 v3 | v3.16.1 | BSL-1.0 |
| Property tests | RapidCheck | `2c3c4365aca21ef4e612768fdc95b1ce8b39a651` | BSD-2 |
| Бенчмарки | nanobench | v4.6.0 | MIT |
| Параллелизм | Taskflow | v4.1.0 | MIT |
| SIMD (позже) | xsimd | 14.3.0 | BSD-3 |
| Сборка | CMake ≥ 3.28, C++23, FetchContent | — | — |

Опционально позже: Accelerate vDSP как backend FFT на macOS, PFFFT, Kitty/Sixel pixel-режим, notcurses (если пиксельная графика станет обязательной).

### CMake FetchContent (configure проверен, сборка нет)

```cmake
cmake_minimum_required(VERSION 3.28)
project(signals LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(ftxui     GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git GIT_TAG v7.0.3  GIT_SHALLOW TRUE)
FetchContent_Declare(cli11     GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git        GIT_TAG v2.7.2  GIT_SHALLOW TRUE)
FetchContent_Declare(json      GIT_REPOSITORY https://github.com/nlohmann/json.git         GIT_TAG v3.12.0 GIT_SHALLOW TRUE)
FetchContent_Declare(doctest   GIT_REPOSITORY https://github.com/doctest/doctest.git       GIT_TAG v2.5.3  GIT_SHALLOW TRUE)

# Header-only sources without a usable CMake target: SOURCE_SUBDIR points to a non-existent dir
# so MakeAvailable only populates the sources.
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.26     GIT_SHALLOW TRUE SOURCE_SUBDIR _none)
FetchContent_Declare(dr_libs   GIT_REPOSITORY https://github.com/mackron/dr_libs.git   GIT_TAG wav-0.14.6  GIT_SHALLOW TRUE SOURCE_SUBDIR _none)
FetchContent_Declare(pocketfft GIT_REPOSITORY https://github.com/mreineck/pocketfft.git
                     GIT_TAG 4d865cba24359b05b37da4e75aee24498c3b78e7 SOURCE_SUBDIR _none)  # branch cpp, no releases
FetchContent_Declare(mp        GIT_REPOSITORY https://github.com/boostorg/multiprecision.git GIT_TAG Boost_1_92_0 GIT_SHALLOW TRUE SOURCE_SUBDIR _none)

set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
FetchContent_Declare(eigen GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git GIT_TAG 5.0.1 GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(ftxui cli11 json doctest miniaudio dr_libs pocketfft mp eigen)
```

Не вошло в проверенный configure (теги взяты с GitHub, но не клонировались): Catch2 v3.16.1, Taskflow v4.1.0, nanobench v4.6.0, {fmt} 12.2.0, xsimd 14.3.0, RapidCheck `2c3c4365aca21ef4e612768fdc95b1ce8b39a651`. Использование Boost.Multiprecision в режиме `BOOST_MP_STANDALONE` (Boost.Math тоже standalone) — не проверено.

---

## Что не удалось проверить

- Сборка и запуск скетчей FTXUI / miniaudio (блокировка сборки внешнего кода); совместимость Eigen `PolynomialSolver` с типами Boost.Multiprecision; `BOOST_MP_STANDALONE`.
- Xcode 26.x / более новый Apple Clang; GCC и Linux (в том числе `std::execution::par` через TBB).
- Мышь SGR и true colour для iTerm2, kitty, WezTerm, Ghostty (общеизвестно, но не подтверждено документацией в сессии); мышь в Terminal.app; Kitty graphics в Terminal.app.
- Форматы libsndfile (страница не прочитана); точность `drwav` на экзотических EXTENSIBLE-файлах.
- Скорость: FFT-библиотеки, свёртка, lifting, `PolynomialSolver` для больших p.
- Дистрибутивные версии CMake/GCC; FLINT/Arb, MPSolve; TermOx, Upp::Tui.
