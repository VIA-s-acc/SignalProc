pipeline "vanishing" v1

node x   = samples(values="{{x}}", rate=32)
node w   = dwt(wavelet=haar, levels=1, ext=symmetric)
node g   = gain(db=-300)
node mid = cut(begin=8, end=12)

x -> w
w.d1 -> g -> mid
probe mid as "d1"
