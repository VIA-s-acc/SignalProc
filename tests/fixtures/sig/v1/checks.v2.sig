pipeline "checks" v2

node x   = samples(values="4 6 10 12 8 6 5 5", rate=8)
node h0  = convolve(filter=haar.h0, method=direct)
node h1  = convolve(filter=haar.h1, method=direct)
node a   = downsample(m=2, anti_alias=no, phase=1)
node d   = downsample(m=2, anti_alias=no, phase=1)
node ua  = upsample(m=2, anti_image=no)
node ud  = upsample(m=2, anti_image=no)
node f0  = convolve(filter=haar.f0, method=direct)
node f1  = convolve(filter=haar.f1, method=direct)
node sum = add()
node pr  = check(kind=pr, tol=1e-12, delay=auto)
node ok  = assert(expr="a.passed && a.max_abs < 1e-9")

x -> h0 -> a -> ua -> f0 -> sum.a
x -> h1 -> d -> ud -> f1 -> sum.b
x -> pr.ref
sum -> pr.in
pr -> ok.a
probe sum as "x̂"
probe pr as "pr"
probe ok as "ok"
