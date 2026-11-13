pipeline "haar_bank" v1

node x   = samples(values="{{x}}", rate=16)
node h0  = convolve(filter=haar.h0, method=direct)
node h1  = convolve(filter=haar.h1, method=direct)
node a   = downsample(m=2, anti_alias=no, phase=1)
node d   = downsample(m=2, anti_alias=no, phase=1)
node ua  = upsample(m=2, anti_image=no)
node ud  = upsample(m=2, anti_image=no)
node f0  = convolve(filter=haar.f0, method=direct)
node f1  = convolve(filter=haar.f1, method=direct)
node sum = add()

x -> h0 -> a -> ua -> f0 -> sum.a
x -> h1 -> d -> ud -> f1 -> sum.b
node z1  = downsample(m=2, anti_alias=no, phase=1)
node z2  = downsample(m=2, anti_alias=no, phase=1)
node z3  = upsample(m=2, anti_image=no)
node z4  = upsample(m=2, anti_image=no)
x -> z1 -> z3
x -> z2 -> z4
