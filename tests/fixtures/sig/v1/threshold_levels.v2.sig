pipeline "levels" v2

node x = gen(kind=doppler, rate=8000, duration=0.05)
node d = dwt(wavelet=sym4, levels=4, ext=symmetric)
node t = threshold(rule=visu, mode=soft, levels=1..3, sigma=mad_d1)
node i = idwt(wavelet=sym4, ext=symmetric)

x -> d -> t -> i
probe i as "i"
