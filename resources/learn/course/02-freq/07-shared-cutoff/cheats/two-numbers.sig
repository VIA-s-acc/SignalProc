pipeline "shared_cutoff" v2

node g1 = gen(kind=sine, freq=500, amplitude=1, rate=8000, duration=1)
node g2 = gen(kind=sine, freq=3000, amplitude={{a2}}, rate=8000, duration=1)
node x  = add()
node lo = fir(numtaps=101, cutoff=1500, window=hamming, pass_zero=yes)
node hi = fir(numtaps=101, cutoff=1500, window=hamming, pass_zero=no)

g1 -> x.a
g2 -> x.b
x -> lo
x -> hi
probe x as "x"
probe lo as "lo"
probe hi as "hi"
