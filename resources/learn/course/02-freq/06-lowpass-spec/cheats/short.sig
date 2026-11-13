pipeline "lowpass_spec" v1

node g1 = gen(kind=sine, freq=500, amplitude=1, rate=8000, duration=1)
node g2 = gen(kind=sine, freq=3000, amplitude={{a2}}, rate=8000, duration=1)
node x  = add()
node y  = fir(numtaps=9, cutoff=1500, window=hamming, pass_zero=yes)

g1 -> x.a
g2 -> x.b
x -> y
probe x as "x"
probe y as "y"
