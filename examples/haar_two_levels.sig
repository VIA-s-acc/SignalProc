pipeline "haar_two_levels" v1

node x    = samples(values="4 6 10 12 8 6 5 5", rate=8)
node h0   = convolve(filter=haar.h0, method=direct)
node h1   = convolve(filter=haar.h1, method=direct)
node a    = downsample(m=2, anti_alias=no, phase=1)
node d    = downsample(m=2, anti_alias=no, phase=1)
node h00  = convolve(filter=haar.h0, method=direct)
node h01  = convolve(filter=haar.h1, method=direct)
node aa   = downsample(m=2, anti_alias=no, phase=1)
node ad   = downsample(m=2, anti_alias=no, phase=1)
node uaa  = upsample(m=2, anti_image=no)
node uad  = upsample(m=2, anti_image=no)
node f00  = convolve(filter=haar.f0, method=direct)
node f01  = convolve(filter=haar.f1, method=direct)
node a2   = add()
node ua   = upsample(m=2, anti_image=no)
node ud   = upsample(m=2, anti_image=no)
node f0   = convolve(filter=haar.f0, method=direct)
node f1   = convolve(filter=haar.f1, method=direct)
node sum  = add()
node pr   = check(kind=pr, tol=1e-12, delay=auto)

x -> h0 -> a -> h00 -> aa -> uaa -> f00 -> a2.a
a -> h01 -> ad -> uad -> f01 -> a2.b
a2 -> ua -> f0 -> sum.a
x -> h1 -> d -> ud -> f1 -> sum.b
x -> pr.ref
sum -> pr.in
probe aa as "aa"
probe ad as "ad"
probe d as "d"
