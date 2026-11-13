pipeline "chirp_ridge" v1

node x    = chirp(method=linear, f0={{f0}}, f1={{f1}}, rate=1000, duration=1)
node r    = sst_ridge(lo=30, hi=480, voices=32, mu=6)
node c    = cut(begin={{k}}, end=1000)
node pick = downsample(m=1000, anti_alias=no)

x -> r
r.freq -> c -> pick
probe r.freq as "ridge, Hz"
probe pick as "f"
