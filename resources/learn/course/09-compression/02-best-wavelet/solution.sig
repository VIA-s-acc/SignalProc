pipeline "best_wavelet" v1

node x = gen(kind=blocks, rate={{n}}, duration=1)
node wh = dwt(wavelet=haar, levels=5, ext=periodization)
node kh = keep_largest(mode=percent, amount=5, keep_approx=yes)
node ih = idwt()
node wd = dwt(wavelet=db4, levels=5, ext=periodization)
node kd = keep_largest(mode=percent, amount=5, keep_approx=yes)
node id = idwt()
node ws = dwt(wavelet=sym8, levels=5, ext=periodization)
node ks = keep_largest(mode=percent, amount=5, keep_approx=yes)
node is = idwt()
node wb = dwt(wavelet=bior4.4, levels=5, ext=periodization)
node kb = keep_largest(mode=percent, amount=5, keep_approx=yes)
node ib = idwt()

x -> wh -> kh -> ih
x -> wd -> kd -> id
x -> ws -> ks -> is
x -> wb -> kb -> ib
probe ih as "haar"
probe id as "db4"
probe is as "sym8"
probe ib as "bior4.4"
