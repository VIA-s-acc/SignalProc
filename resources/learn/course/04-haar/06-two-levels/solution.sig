pipeline "haar_two_levels" v1

node x   = samples(values="{{x}}", rate=16)
node h0 = convolve(filter=haar.h0, method=direct)
node h1 = convolve(filter=haar.h1, method=direct)
node g0 = convolve(filter=haar.h0, method=direct)
node g1 = convolve(filter=haar.h1, method=direct)
node a1 = downsample(m=2, anti_alias=no, phase=1)
node d1 = downsample(m=2, anti_alias=no, phase=1)
node a2 = downsample(m=2, anti_alias=no, phase=1)
node d2 = downsample(m=2, anti_alias=no, phase=1)
node ua2 = upsample(m=2, anti_image=no)
node ud2 = upsample(m=2, anti_image=no)
node ua1 = upsample(m=2, anti_image=no)
node ud1 = upsample(m=2, anti_image=no)
node k0 = convolve(filter=haar.f0, method=direct)
node k1 = convolve(filter=haar.f1, method=direct)
node f0 = convolve(filter=haar.f0, method=direct)
node f1 = convolve(filter=haar.f1, method=direct)
node s2  = add()
node sum = add()

x -> h0 -> a1 -> g0 -> a2 -> ua2 -> k0 -> s2.a
a1 -> g1 -> d2 -> ud2 -> k1 -> s2.b
s2 -> ua1 -> f0 -> sum.a
x -> h1 -> d1 -> ud1 -> f1 -> sum.b
probe a2 as "a2"
probe d2 as "d2"
probe d1 as "d1"
probe sum as "x̂"
