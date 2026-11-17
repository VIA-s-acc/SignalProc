pipeline "w_nonzeros" v2

node e = gen(kind=impulse, rate={{n}}, duration=1)
node d = delay(samples=3)
node c = cut(begin=0, end={{n}})
node w = dwt(wavelet=haar, levels={{j}}, ext=periodization)

e -> d -> c -> w
