pipeline "partial_recon" v1

node x = samples(values="{{x}}", rate=64)
node w = dwt(wavelet={{wv}}, levels=3, ext=periodization)
node e = band_edit(enabled=all)
node y = idwt()

x -> w -> e -> y
probe y as "y"
