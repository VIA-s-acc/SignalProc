pipeline "shift_variance" v1

node x   = samples(values="{{x}}", rate=16)
node s   = shift(samples=1, mode=circular)
node w1  = dwt(wavelet=haar, levels=1, ext=periodization)
node w2  = dwt(wavelet=haar, levels=1, ext=periodization)
node q1  = square()
node q2  = square()
node c1  = cumsum()
node c2  = cumsum()
node l1  = cut(begin=7, end=8)
node l2  = cut(begin=7, end=8)
node r   = binary(op=divide)
node pct = affine(scale=100)

x -> w1
x -> s -> w2
w1.d1 -> q1 -> c1 -> l1 -> r.b
w2.d1 -> q2 -> c2 -> l2 -> r.a
r -> pct
probe l1 as "E(d1)"
probe l2 as "E(d1 shifted)"
probe pct as "%"
