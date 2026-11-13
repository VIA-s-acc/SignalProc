pipeline "haar_bank" v1

node x   = samples(values="{{x}}", rate=8)
node h0  = convolve(filter=haar.h0, method=direct)
node a   = downsample(m=2, anti_alias=no, phase=1)
node ua  = upsample(m=2, anti_image=no)
node f0  = convolve(filter=haar.f0, method=direct)
node sum = gain(db=6.0206)

x -> h0 -> a -> ua -> f0 -> sum
