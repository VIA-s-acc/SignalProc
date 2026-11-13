pipeline "parseval" v1

node x = samples(values="{{x}}", rate=16)
node w = dwt(wavelet={{wv}}, levels=1, ext=periodization)
node m = concat()

x -> w
w.a -> m.a
w.d1 -> m.b
probe m as "Ax"
