pipeline "haar_energy" v1

node x   = samples(values="{{x}}", rate=8)
node h0  = convolve(filter=haar.h0, method=direct)
node h1  = convolve(filter=haar.h1, method=direct)
node a   = downsample(m=2, anti_alias=no, phase=1)
node d   = downsample(m=2, anti_alias=no, phase=1)
node dq  = square()
node dc  = cumsum()
node dl  = cut(begin=3, end=4)
node du  = upsample(m=2, anti_image=no)
node dr  = cut(begin=0, end=1)
node xq  = square()
node xc  = cumsum()
node xl  = cut(begin=7, end=8)
node r   = binary(op=divide)
node pct = affine(scale=100)

x -> h0 -> a
x -> h1 -> d
d -> dq -> dc -> dl -> du -> dr -> r.a
x -> xq -> xc -> xl -> r.b
r -> pct
probe a as "a"
probe d as "d"
probe pct as "%"
