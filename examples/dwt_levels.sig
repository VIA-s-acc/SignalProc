pipeline "dwt_levels" v1

node x    = gen(kind=heavisine, rate=8000, duration=0.25, amplitude=0.5)
node dwt  = dwt(wavelet=db4, levels=3, ext=symmetric)
node idwt = idwt()
node pr   = check(kind=pr, tol=1e-10)

x -> dwt -> idwt -> pr.in
x -> pr.ref
probe dwt.a as "a3"
probe dwt.d3 as "d3"
probe dwt.d2 as "d2"
probe dwt.d1 as "d1"
