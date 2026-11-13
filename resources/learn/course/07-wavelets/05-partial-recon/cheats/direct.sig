pipeline "partial_recon" v1

node x = samples(values="{{x}}", rate=64)
node w = dwt(wavelet={{wv}}, levels=3, ext=periodization)
node y = idwt()

x -> w -> y
probe y as "y"
