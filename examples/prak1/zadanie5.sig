pipeline "prak1_zadanie5" v1

# Задание 5: x[n] = sin(0.15πn) + 0.35·sin(0.80πn), n = 0..127.
# Частота ω = 2πf/fs: при fs = 1000 Гц ω = 0.15π соответствует 75 Гц, ω = 0.80π соответствует 400 Гц.
# 0.128 с при 1000 Гц дают ровно 128 отсчётов.
node s1 = gen(kind=sine, freq=75, rate=1000, duration=0.128)
node s2 = gen(kind=sine, freq=400, amplitude=0.35, rate=1000, duration=0.128)
node x  = add()
# Для фильтра из двух отводов fir_taps совпадает с np.convolve(x, h, mode="same")
node y0 = fir_taps(taps="0.5 0.5")
node y1 = fir_taps(taps="0.5 -0.5")
node px = spectrum_peaks(count=2, segment=128, prominence_db=3)
node p0 = spectrum_peaks(count=2, segment=128, prominence_db=3)
node p1 = spectrum_peaks(count=2, segment=128, prominence_db=3)

s1 -> x.a
s2 -> x.b
x -> y0
x -> y1
x -> px
y0 -> p0
y1 -> p1
probe x as "вход x"
probe y0 as "выход ФНЧ y0"
probe y1 as "выход ФВЧ y1"
probe px as "пики x"
probe p0 as "пики y0"
probe p1 as "пики y1"
