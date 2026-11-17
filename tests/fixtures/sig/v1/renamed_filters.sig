pipeline "renamed_filters" v1
node x = gen(kind=white, rate=8000, duration=0.02, seed=3)
node d = gen(kind=sine, freq=300, rate=8000, duration=0.02)
node l = lms(taps=16, rule=nlms, mu=0.5)
node sg = savgol(window=11, polyorder=3)
node md = median(kernel=5)
node wn = wiener(size=5)
node ma = moving_average(length=5, centred=yes)
node mu = multiply(factor=0.5)
node dc = dc_offset(value=0.25)
node it = impulse_train(period=40, offset=10, rate=8000, duration=0.02)
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
