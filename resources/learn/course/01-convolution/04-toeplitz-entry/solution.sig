pipeline "toeplitz_entry" v1

node e = gen(kind=impulse, rate=8, duration=0.5)
node d = delay(samples={{j}})
node p = pad(after=6)
node f = fir_taps(taps="{{h}}")
node r = cut(begin={{i}})
node t = cut(begin=0, end=1)

e -> d -> p -> f -> r -> t
probe t as "t"
