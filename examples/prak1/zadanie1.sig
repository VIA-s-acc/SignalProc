pipeline "prak1_zadanie1" v1

# Задание 1: полная свёртка x = [2, -1, 3, 1] с h0 = [1/2, 1/2] и h1 = [1/2, -1/2].
# fir_taps обрезает свёртку до длины входа, поэтому вход дополнен одним нулём:
# 4 + 1 = 5 отсчётов, столько же, сколько в полной свёртке 4 + 2 - 1.
node x  = samples(values="2 -1 3 1", rate=1)
node xp = pad(after=1)
node y0 = fir_taps(taps="0.5 0.5")
node y1 = fir_taps(taps="0.5 -0.5")

x -> xp
xp -> y0
xp -> y1
probe y0 as "y0 = x*h0"
probe y1 as "y1 = x*h1"
