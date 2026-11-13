pipeline "keep_few" v1

node x    = gen(kind=doppler, rate=1024, duration=1)
node w    = dwt(wavelet=db4, levels=5, ext=periodization)
node keep = keep_largest(mode=percent, amount=8, keep_approx=yes)
node i    = idwt()
node y    = gain(db=0)

x -> w -> i -> y
w -> keep
probe keep as "kept"
probe y as "y"
