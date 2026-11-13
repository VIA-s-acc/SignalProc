pipeline "h0_db2" v1

node imp = samples(values="1 0 0 0", rate=8)
node h0  = convolve(filter=db2.h0, method=direct)
node taps = cut(begin=0, end=4)

imp -> h0 -> taps
probe taps as "h0"
