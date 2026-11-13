pipeline "haar_entry" v1

node e = gen(kind=impulse, rate=8, duration=1)
node d = delay(samples={{c}})
node k = cut(begin=0, end=8)
node w = dwt(wavelet=haar, levels=1, ext=periodization)
node m = concat()
node r = cut(begin={{r}})
node t = cut(begin=0, end=1)

e -> d -> k -> w
w.a -> m.a
w.d1 -> m.b
m -> r -> t
probe t as "t"
