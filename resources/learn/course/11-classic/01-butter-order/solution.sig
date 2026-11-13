pipeline "butter_order" v1

node sp = gen(kind=sine, freq=1000, rate=8000, duration=1)
node ss = gen(kind=sine, freq=1500, rate=8000, duration=1)
node bp = iir(family=butterworth, type=lowpass, order=10, cutoff=1000)
node bs = iir(family=butterworth, type=lowpass, order=10, cutoff=1000)
node cp = iir(family=butterworth, type=lowpass, order=9, cutoff=1000)
node cs = iir(family=butterworth, type=lowpass, order=9, cutoff=1000)

sp -> bp
ss -> bs
sp -> cp
ss -> cs
probe bp as "edge, N"
probe bs as "stop, N"
probe cp as "edge, N-1"
probe cs as "stop, N-1"
