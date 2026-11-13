pipeline "swt_shift" v1

node x  = samples(values="{{x}}", rate=16)
node sa = shift(samples=1, mode=circular)
node w  = swt(wavelet=haar, levels=2)
node p  = gain(db=0)
node sb = shift(samples=1, mode=circular)
node q  = gain(db=0)

x -> w
w.d1 -> sa -> p
w.d1 -> sb -> q
probe p as "p"
probe q as "q"
