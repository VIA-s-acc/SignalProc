pipeline "db2_bank" v1

node x   = samples(values="{{x}}", rate=16)
node w   = dwt(wavelet=db2, levels=1, ext=periodization)
node i   = idwt()
node sum = gain(db=0)

x -> w -> i -> sum
