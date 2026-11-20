pipeline "bands" v2

node x = gen(kind=sine, freq=440, rate=8000, duration=0.02)
node d = dwt(wavelet=db2, levels=3, ext=symmetric)
node y = gain(db=0)

x -> d
d.d1 -> y
probe y as "d1"
probe d.a as "a3"
