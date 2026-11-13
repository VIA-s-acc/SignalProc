pipeline "fir_delay" v1

node x = gen(kind=impulse, rate=8000, duration=0.05)
node y = fir(numtaps={{L}}, cutoff=1000, window=hamming, pass_zero=yes)

x -> y
probe y as "y"
