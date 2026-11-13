pipeline "mra_sum" v1

node x   = samples(values="{{x}}", rate=16)
node w   = dwt(wavelet=haar, levels=3, ext=periodization)
node ma  = mra(component=a3)
node m3  = mra(component=d3)
node m2  = mra(component=d2)
node m1  = mra(component=d1)
node s1  = add()
node s2  = add()
node sum = add()

x -> w
w -> ma
w -> m3
w -> m2
w -> m1
ma -> s1.a
m3 -> s1.b
m2 -> s2.a
m1 -> s2.b
s1 -> sum.a
s2 -> sum.b
probe sum as "sum"
