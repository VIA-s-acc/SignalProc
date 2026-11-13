pipeline "best_basis" v1

node x  = gen(kind=heavisine, rate=256, duration=1)
node w  = wpt(wavelet=db4, levels=4, basis=best, ext=periodization)
node st = coeff_stats()

x -> w -> st
probe w as "best basis"
