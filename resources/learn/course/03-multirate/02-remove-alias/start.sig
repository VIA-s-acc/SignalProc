pipeline "anti_alias" v1

node lo = gen(kind=sine, freq=500, rate=8000, duration=1)
node hi = gen(kind=sine, freq=3000, amplitude={{a}}, rate=8000, duration=1)
node x  = add()
node d  = downsample(m=2, anti_alias=no)
node y  = gain(db=0)

lo -> x.a
hi -> x.b
x -> d -> y
probe x as "x"
probe y as "y"
