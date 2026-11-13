pipeline "zero_phase" v1

node low  = gen(kind=sine, freq=300, amplitude=1, rate=8000, duration=1)
node high = gen(kind=sine, freq=2000, amplitude={{a}}, rate=8000, duration=1)
node x    = add()
node y    = gain(db=0)

low -> x.a
high -> x.b
x -> y
probe x as "x"
probe y as "y"
