pipeline "multiline" v2

node x = gen(kind=sine, freq=440, rate=8000, duration=0.01)
node g = gain(db=-3)

x -> g
probe g as "g"
