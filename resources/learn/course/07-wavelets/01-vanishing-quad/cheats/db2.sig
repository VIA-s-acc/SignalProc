pipeline "vanishing" v1

node x   = samples(values="{{x}}", rate=32)
node w   = dwt(wavelet=db2, levels=1, ext=symmetric)
node mid = cut(begin=8, end=12)

x -> w
w.d1 -> mid
probe mid as "d1"
