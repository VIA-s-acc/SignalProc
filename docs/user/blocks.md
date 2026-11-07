# Блоки конвейера

Сгенерировано командой `signals blocks --markdown`. Не правьте вручную: тест сверяет файл с кодом.

| Блок | Название | Входы | Выходы | Параметры |
|---|---|---|---|---|
| `wav` | WAV | — | `out`: signal | `path` = — |
| `gen` | Генератор | — | `out`: signal | `kind` = sine (sine, sweep, impulse, step, white, pink, harmonics, doppler, blocks, bumps, heavisine); `freq` = 440; `f1` = 8000; `amplitude` = 1; `rate` = 44100 (1…768000); `duration` = 1 (0…3600); `seed` = 1 |
| `gain` | Усиление | `in`: signal | `out`: signal | `db` = 0 |
| `normalize` | Нормировка | `in`: signal | `out`: signal | `kind` = peak (peak, rms); `target_dbfs` = -1 |
| `remove_dc` | Без DC | `in`: signal | `out`: signal | — |
| `clip` | Ограничение | `in`: signal | `out`: signal | `limit` = 1 (0…1e+06) |
| `quantize` | Квантование | `in`: signal | `out`: signal | `bits` = 16 (1…32); `dither` = yes (yes, no); `seed` = 24301 |
| `mulaw` | μ-закон | `in`: signal | `out`: signal | `direction` = encode (encode, decode); `mu` = 255 (1…65535) |
| `split` | Каналы | `in`: signal | `l`: signal; `r`: signal (необязательный) | `mode` = lr (lr, channel); `channel` = 0 |
| `mixdown` | В моно | `in`: signal | `out`: signal | — |
| `mid_side` | M/S | `in`: signal | `out`: signal | `direction` = encode (encode, decode) |
| `delay` | Задержка | `in`: signal | `out`: signal | `samples` = 0 |
| `reverse` | Разворот | `in`: signal | `out`: signal | — |
| `cut` | Вырезка | `in`: signal | `out`: signal | `begin` = 0; `end` = 0 |
| `downsample` | Прореживание ↓M | `in`: signal | `out`: signal | `m` = 2 (1…1024); `attenuation_db` = 80 (10…200); `anti_alias` = yes (yes, no) |
| `upsample` | Растяжение ↑M | `in`: signal | `out`: signal | `m` = 2 (1…1024); `attenuation_db` = 80 (10…200) |
| `resample` | Ресемплинг L/M | `in`: signal | `out`: signal | `l` = 1 (1…1024); `m` = 1 (1…1024); `attenuation_db` = 80 (10…200) |
| `convolve` | Свёртка | `in`: signal | `out`: signal | `filter` = db2.h0; `method` = auto (auto, direct, fft) |
| `dwt` | DWT | `in`: signal | `coeffs`: coeffs | `wavelet` = db2; `levels` = 0 (0…64); `ext` = symmetric (symmetric, periodization, zero, constant, smooth, periodic, reflect, antisymmetric, antireflect) |
| `idwt` | IDWT | `in`: coeffs | `out`: signal | `wavelet` = auto; `ext` = auto (auto, symmetric, periodization, zero, constant, smooth, periodic, reflect, antisymmetric, antireflect) |
| `swt` | SWT | `in`: signal | `coeffs`: coeffs | `wavelet` = db2; `levels` = 0 (0…32); `norm` = no (no, yes) |
| `lift53` | Лифтинг 5/3 | `in`: signal | `coeffs`: coeffs | `levels` = 4 (1…30); `integer` = no (no, yes) |
| `ilift53` | Обратный лифтинг 5/3 | `in`: coeffs | `out`: signal | — |
| `iswt` | ISWT | `in`: coeffs | `out`: signal | `wavelet` = auto |
| `threshold` | Порог | `in`: coeffs | `coeffs`: coeffs | `rule` = bayes (bayes, visu, sure, minimax); `mode` = soft (soft, hard, garrote); `levels` = 1..64; `sigma` = mad_d1 |
| `band_edit` | Правка полос | `in`: coeffs | `coeffs`: coeffs | `enabled` = all; `gains_db` = — |
| `band_energy` | Энергия полос | `in`: coeffs | `report`: report | — |
| `envelope` | Огибающая | `in`: signal | `out`: signal | — |
| `inst_freq` | Мгновенная частота | `in`: signal | `out`: signal | — |
| `cepstrum` | Кепстр | `in`: signal | `out`: signal | — |
| `dct` | DCT-II | `in`: signal | `out`: signal | — |
| `idct` | Обратное DCT | `in`: signal | `out`: signal | — |
| `xcorr` | Корреляция | `a`: signal; `b`: signal | `out`: signal | `mode` = full (full, same, valid); `normalize` = no (no, yes) |
| `denoise` | Шумоподавление | `in`: signal | `out`: signal | `wavelet` = sym8; `levels` = 0 (0…32); `rule` = bayes (bayes, visu, sure, minimax); `mode` = soft (soft, hard); `shifts` = 1 (1…64) |
| `thd` | THD | `in`: signal | `report`: report | `harmonics` = 5 (2…20) |
| `onsets` | Онсеты | `in`: signal | `report`: report | `n` = 1024 (64…65536); `hop` = 256 (1…65536) |
| `export` | Выгрузка CSV / JSON | `in`: signal | `report`: report | `path` = out.csv; `format` = csv (csv, json) |
| `export_report` | Выгрузка отчёта | `in`: report | `report`: report | `path` = report.json; `format` = json (csv, json) |
| `check` | Проверка | `ref`: signal; `in`: signal | `report`: report | `kind` = pr (pr, error, snr); `tol` = 1e-12 |
| `wav_out` | Запись | `in`: signal | `report`: report | `path` = out.wav; `bits` = 24 (16, 24, 32, f32); `dither` = tpdf (none, tpdf); `seed` = 24301 |
| `listen` | Прослушивание | `in`: signal | — | — |
| `lms` | Адаптивный фильтр LMS/NLMS | `x`: signal; `d`: signal | `e`: signal; `y`: signal | `taps` = 32 (1…4096); `mu` = 0.1 (1e-06…2); `rule` = nlms (nlms, lms) |
