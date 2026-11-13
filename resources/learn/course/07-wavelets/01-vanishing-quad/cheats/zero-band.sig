pipeline "vanishing" v1

node x   = samples(values="{{x}}", rate=32)
node w   = dwt(wavelet=haar, levels=1, ext=symmetric)
node e   = band_edit(enabled=a)
node mid = cut(begin=8, end=12)

x -> w -> e
e.d1 -> mid
probe mid as "d1"
