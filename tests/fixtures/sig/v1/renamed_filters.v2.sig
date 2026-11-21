pipeline "renamed_filters" v2

node x  = gen(kind=white, rate=8000, duration=0.02, seed=3)
node d  = gen(kind=sine, freq=300, rate=8000, duration=0.02)
node l  = lms(numtaps=16, mu=0.5, rule=nlms)
node sg = savgol(length=11, polyorder=3)
node md = median(length=5)
node wn = wiener(length=5)
node ma = moving_average(length=5, compensate=yes)
node mu = multiply(scale=0.5)
node dc = dc_offset(offset=0.25)
node it = impulse_train(period=40, begin=10, rate=8000, duration=0.02)

x -> l.x
d -> l.d
x -> sg
x -> md
x -> wn
x -> ma
x -> mu
x -> dc
probe l.e as "e"
probe sg as "sg"
probe md as "md"
probe wn as "wn"
probe ma as "ma"
probe mu as "mu"
probe dc as "dc"
probe it as "it"
