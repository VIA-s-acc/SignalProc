pipeline "h0_53" v1

node imp = samples(values="1 0 0 0 0 0", rate=8)
node h0  = convolve(filter=bior2.2.h0, method=direct)
node taps = cut(begin=1, end=6)

imp -> h0 -> taps
probe taps as "h0"
