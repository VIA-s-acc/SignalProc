# Блоки конвейера

Сгенерировано командой `signals blocks --markdown`. Не правьте вручную: тест сверяет файл с кодом.

| Блок | Название | Входы | Выходы | Параметры |
|---|---|---|---|---|
| `wav` | WAV | — | `out`: signal | `path` = — |
| `samples` | Отсчёты | — | `out`: signal | `values` = 4 6 10 12 8 6 5 5; `rate` = 8 (1…768000) |
| `add` | Сумматор ⊕ | `a`: signal; `b`: signal | `out`: signal | `op` = add (add, sub) |
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
| `downsample` | Прореживание ↓M | `in`: signal | `out`: signal | `m` = 2 (1…1024); `attenuation_db` = 80 (10…200); `anti_alias` = yes (yes, no); `phase` = 0 (0…1023) |
| `upsample` | Растяжение ↑M | `in`: signal | `out`: signal | `m` = 2 (1…1024); `attenuation_db` = 80 (10…200); `anti_image` = yes (yes, no) |
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
| `check` | Проверка | `ref`: signal; `in`: signal | `report`: report | `kind` = pr (pr, error, snr); `tol` = 1e-12; `delay` = 0 |
| `wav_out` | Запись | `in`: signal | `report`: report | `path` = out.wav; `bits` = 24 (16, 24, 32, f32); `dither` = tpdf (none, tpdf); `seed` = 24301 |
| `listen` | Прослушивание | `in`: signal | — | — |
| `pad` | Дополнение нулями | `in`: signal | `out`: signal | `before` = 0; `after` = 0 |
| `concat` | Склейка | `a`: signal; `b`: signal | `out`: signal | — |
| `multiply` | Умножитель ⊗ | `a`: signal; `b`: signal (необязательный) | `out`: signal | `factor` = 1 (-1e+06…1e+06) |
| `window` | Оконная функция | `in`: signal | `out`: signal | `kind` = hann (hann, hamming, blackman, kaiser, tukey); `beta` = 8.6 (0…100); `alpha` = 0.5 (0…1) |
| `fade` | Плавное нарастание и затухание | `in`: signal | `out`: signal | `fade_in` = 0.01s; `fade_out` = 0.01s; `curve` = linear (linear, cosine) |
| `invert` | Смена полярности | `in`: signal | `out`: signal | — |
| `abs` | Модуль \|x\| | `in`: signal | `out`: signal | — |
| `square` | Квадрат x² | `in`: signal | `out`: signal | — |
| `diff` | Первая разность | `in`: signal | `out`: signal | — |
| `cumsum` | Накопленная сумма | `in`: signal | `out`: signal | — |
| `softclip` | Мягкое ограничение tanh | `in`: signal | `out`: signal | `drive` = 1 (0.01…1000) |
| `dc_offset` | Постоянное смещение | `in`: signal | `out`: signal | `value` = 0 (-1e+06…1e+06) |
| `trim_silence` | Обрезка тишины | `in`: signal | `out`: signal | `threshold_db` = -60 (-200…0) |
| `add_noise` | Добавить шум | `in`: signal | `out`: signal | `color` = white (white, pink, brown); `snr_db` = 20 (-100…200); `seed` = 1 (0…1e+15) |
| `chirp` | Чирп | — | `out`: signal | `method` = linear (linear, log); `f0` = 100 (0…1e+06); `f1` = 8000 (0…1e+06); `amplitude` = 1 (-1e+06…1e+06); `rate` = 44100 (1…768000); `duration` = 1 (0…3600) |
| `osc` | Меандр, пила, треугольник | — | `out`: signal | `kind` = square (square, sawtooth, triangle, pulses); `freq` = 440 (0…1e+06); `duty` = 0.5 (0…1); `width` = 1 (0…1); `amplitude` = 1 (-1e+06…1e+06); `rate` = 44100 (1…768000); `duration` = 1 (0…3600) |
| `iir` | БИХ-фильтр | `in`: signal | `out`: signal | `family` = butterworth (butterworth, cheby1, cheby2, ellip, bessel); `type` = lowpass (lowpass, highpass, bandpass, bandstop); `order` = 4 (1…32); `cutoff` = 1000; `rp` = 1 (0.01…30); `rs` = 40 (1…200); `zero_phase` = no (no, yes) |
| `fir` | КИХ-фильтр (метод окна) | `in`: signal | `out`: signal | `numtaps` = 101 (1…8191); `cutoff` = 1000; `window` = hamming (hamming, hann, blackman, kaiser, boxcar); `beta` = 8.6 (0…50); `pass_zero` = yes (yes, no); `compensate` = no (no, yes) |
| `median` | Медианный фильтр | `in`: signal | `out`: signal | `kernel` = 5 (1…4095) |
| `moving_average` | Скользящее среднее | `in`: signal | `out`: signal | `length` = 5 (1…65536); `centred` = no (no, yes) |
| `preemphasis` | Предыскажение | `in`: signal | `out`: signal | `coeff` = 0.97 (0…1) |
| `deemphasis` | Снятие предыскажения | `in`: signal | `out`: signal | `coeff` = 0.97 (0…0.9999) |
| `lift97` | Лифтинг 9/7 | `in`: signal | `coeffs`: coeffs | `levels` = 4 (1…30) |
| `ilift97` | Обратный лифтинг 9/7 | `in`: coeffs | `out`: signal | — |
| `mra` | Компонента КМА | `in`: coeffs | `out`: signal | `component` = d1 |
| `merge` | Сборка стерео | `l`: signal; `r`: signal | `out`: signal | — |
| `swap` | Обмен L↔R | `in`: signal | `out`: signal | — |
| `pan` | Панорама | `in`: signal | `out`: signal | `position` = 0 (-1…1); `law` = constant_power (constant_power, linear) |
| `duplicate` | Размножение каналов | `in`: signal | `out`: signal | `channels` = 2 (1…64) |
| `stats` | Статистика сигнала | `in`: signal | `report`: report | — |
| `loudness` | Громкость (RMS по 400 мс) | `in`: signal | `report`: report | `window` = 0.4s; `hop` = 0.1s |
| `lms` | Адаптивный фильтр LMS/NLMS | `x`: signal; `d`: signal | `e`: signal; `y`: signal | `taps` = 32 (1…4096); `mu` = 0.1 (1e-06…2); `rule` = nlms (nlms, lms) |
| `wiener` | Винеровский фильтр | `in`: signal | `out`: signal | `size` = 5 (1…1001); `noise` = 0 (0…1e+06) |
| `spectral_subtract` | Спектральное вычитание | `x`: signal; `noise`: signal (необязательный) | `out`: signal | `alpha` = 2 (0…10); `beta` = 0.01 (0…1); `n` = 512 (16…16384); `noise_s` = 0.25 (0.001…60) |
| `deconvolve` | Деконволюция | `y`: signal; `h`: signal | `out`: signal | `snr_db` = 30 (-20…120) |
