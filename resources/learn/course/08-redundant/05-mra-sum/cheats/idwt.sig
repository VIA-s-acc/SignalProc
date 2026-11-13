pipeline "mra_sum" v1

node x   = samples(values="{{x}}", rate=16)
node w   = dwt(wavelet=haar, levels=3, ext=periodization)
node i   = idwt()
node m1  = mra(component=d1)
node m2  = mra(component=d2)
node m3  = mra(component=d3)
node ma  = mra(component=a3)
node sum = gain(db=0)

x -> w -> i -> sum
w -> m1
w -> m2
w -> m3
w -> ma
probe sum as "sum"
