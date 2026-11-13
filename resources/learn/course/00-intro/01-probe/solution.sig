pipeline "probe" v1

node x = gen(kind=sine, freq={{f}}, rate=8000, duration=0.01)
node y = moving_average(length=4)

x -> y
probe y as "y"
