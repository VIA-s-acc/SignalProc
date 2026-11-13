pipeline "peak" v1

node x = gen(kind=sine, freq=1000, amplitude={{a}}, rate=8000, duration=0.01)
node s = stats()

x -> s
probe x as "x"
probe s as "stats"
