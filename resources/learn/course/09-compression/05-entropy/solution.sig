pipeline "entropy" v1

node x  = gen(kind=doppler, rate=1024, duration=1)
node w  = dwt(wavelet=db4, levels=5, ext=periodization)
node q  = coeff_quantize(mode=bits, bits={{b}})
node st = coeff_stats(tol=0.01)

x -> w -> q -> st
probe q as "quantized"
