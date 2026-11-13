pipeline "haar_step" v1

node x  = samples(values="{{x}}", rate=8)
node h0  = convolve(filter=haar.h0, method=direct)
node a  = downsample(m=2, anti_alias=no, phase=1)

x -> h0 -> a
probe a as "a"
