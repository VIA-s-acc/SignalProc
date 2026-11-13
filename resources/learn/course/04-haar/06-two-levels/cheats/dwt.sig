pipeline "haar_two_levels" v1

node x   = samples(values="{{x}}", rate=16)
node w   = dwt(wavelet=haar, levels=2, ext=periodization)
node i   = idwt()
node sum = gain(db=0)

x -> w -> i -> sum
