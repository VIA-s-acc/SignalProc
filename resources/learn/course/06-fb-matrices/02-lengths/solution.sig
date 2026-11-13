pipeline "lengths" v1

node x  = gen(kind=white, rate={{n}}, duration=1)
node wp = dwt(wavelet=db{{p}}, levels=1, ext=periodization)
node ws = dwt(wavelet=db{{p}}, levels=1, ext=symmetric)

x -> wp
x -> ws
