pipeline "swt_shift" v1

node x  = samples(values="{{x}}", rate=16)
node sa = shift(samples=1, mode=circular)
node wa = dwt(wavelet=haar, levels=2, ext=periodization)
node p  = gain(db=0)
node wb = swt(wavelet=haar, levels=2)
node sb = shift(samples=1, mode=circular)
node q  = gain(db=0)

x -> sa -> wa
wa.d1 -> p
x -> wb
wb.d1 -> sb -> q
probe p as "p"
probe q as "q"
