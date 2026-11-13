pipeline "quantize_bits" v1

node x = gen(kind=doppler, rate={{n}}, duration=1)
node w = dwt(wavelet=db4, levels=5, ext=periodization)
node q = coeff_quantize(mode=bits, bits=6)
node i = idwt()
node y = gain(db=0)

x -> w -> q -> i -> y
probe y as "y"
