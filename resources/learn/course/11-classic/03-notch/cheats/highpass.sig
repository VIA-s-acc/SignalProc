pipeline "hum" v1

node tone = gen(kind=sine, freq=440, amplitude=1, rate=8000, duration=1)
node hum  = gen(kind=sine, freq=50, amplitude={{a}}, rate=8000, duration=1)
node x    = add()
node y    = iir(family=butterworth, type=highpass, order=4, cutoff=1000)

tone -> x.a
hum -> x.b
x -> y
probe x as "x"
probe y as "y"
