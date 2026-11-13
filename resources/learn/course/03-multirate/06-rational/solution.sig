pipeline "rational" v1

node x = gen(kind=sine, freq=1000, rate=44100, duration=0.1)
node y = resample(l=160, m=147)

x -> y
probe y as "y"
