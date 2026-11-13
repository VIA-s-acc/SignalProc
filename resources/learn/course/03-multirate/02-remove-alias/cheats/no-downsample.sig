pipeline "anti_alias" v1

node lo = gen(kind=sine, freq=500, rate=8000, duration=1)
node hi = gen(kind=sine, freq=3000, amplitude={{a}}, rate=8000, duration=1)
node x  = add()
node y  = fir(numtaps=101, cutoff=1800, window=hamming)

lo -> x.a
hi -> x.b
x -> y
