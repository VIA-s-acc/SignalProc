pipeline "mad_sigma" v1

node x  = gen(kind={{sig}}, rate=2048, duration=1)
node n  = add_noise(color=white, snr_db={{snr}}, seed={{seed}})
node d  = dwt(wavelet=db4, levels=3)
node a  = abs()
node m  = median(kernel=1027)
node k  = multiply(factor=1.48258)
node c  = cut(begin=513, end=514)
node st = stats()
node e  = add(op=sub)
node se = stats()

x -> n -> d
d.d1 -> a -> m -> k -> c -> st
n -> e.a
x -> e.b
e -> se
probe n as "noisy"
probe d.d1 as "d1"
