pipeline "haar_step" v1

node x  = samples(values="{{x}}", rate=8)
node h1  = convolve(filter=haar.h1, method=direct)
node d  = downsample(m=2, anti_alias=no, phase=1)

x -> h1 -> d
probe d as "d"
