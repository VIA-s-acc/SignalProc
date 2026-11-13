pipeline "long_record" v1

node x = gen(kind=harmonics, freq=220, amplitude=1, rate=8000, duration=4)
node w = gen(kind=white, amplitude={{a}}, rate=8000, duration=4, seed={{seed}})
node n = add()
node d = dwt(wavelet=sym8, levels=0)
node y = idwt()

x -> n.a
w -> n.b
n -> d -> y
probe x as "clean"
probe n as "noisy"
probe y as "y"
