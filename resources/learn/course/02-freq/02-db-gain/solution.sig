pipeline "response_at_075" v1

node x = gen(kind=sine, freq=3000, amplitude=1, rate=8000, duration=1)
node y = convolve(filter={{w}}.h0, method=direct)

x -> y
probe y as "y"
