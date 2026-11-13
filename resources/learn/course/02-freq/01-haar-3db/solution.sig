pipeline "haar_response" v1

node x = gen(kind=sine, freq=2000, amplitude=1, rate=8000, duration=1)
node y = convolve(filter=haar.h0, method=direct)

x -> y
probe y as "y"
