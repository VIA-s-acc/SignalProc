pipeline "bank" v1

node x   = samples(values="{{x}}", rate=8)
node w   = dwt(wavelet=haar, levels=1, ext=periodization)
node i   = idwt()
node sum = gain(db=0)

x -> w -> i -> sum
