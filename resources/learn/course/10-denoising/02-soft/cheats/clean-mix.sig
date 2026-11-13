pipeline "soft_threshold" v1

node x = gen(kind=doppler, rate=2048, duration=1)
node n = add_noise(color=white, snr_db={{snr}}, seed={{seed}})
node w = dwt(wavelet=sym8, levels=5, ext=symmetric)
node t = threshold(rule=bayes, mode=soft)
node i = idwt()
node q = gain(db=-120)
node y = add()

x -> y.a
x -> n -> w -> t -> i -> q -> y.b
