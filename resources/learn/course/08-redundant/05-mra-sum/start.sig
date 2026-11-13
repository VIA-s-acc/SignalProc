pipeline "mra_sum" v1

node x   = samples(values="{{x}}", rate=16)
node w   = dwt(wavelet=haar, levels=3, ext=periodization)
node m1  = mra(component=d1)
node sum = gain(db=0)

x -> w -> m1 -> sum
probe m1 as "d1"
probe sum as "sum"
