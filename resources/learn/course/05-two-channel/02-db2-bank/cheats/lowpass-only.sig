pipeline "db2_bank" v1

node x   = samples(values="{{x}}", rate=16)
node h0  = convolve(filter=db2.h0, method=direct)
node a   = downsample(m=2, anti_alias=no, phase=0)
node ua  = upsample(m=2, anti_image=no)
node f0  = convolve(filter=db2.f0, method=direct)
node sum = gain(db=3.0103)

x -> h0 -> a -> ua -> f0 -> sum
probe a as "a"
probe sum as "x̂"
