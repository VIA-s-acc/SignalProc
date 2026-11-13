pipeline "bayes_shrink" v1

node x = gen(kind=bumps, rate=2048, duration=1)
node n = add_noise(color=white, snr_db=2, seed={{seed}})
node d = dwt(wavelet=db4, levels=6)
node t = threshold(rule=bayes, mode=soft)
node y = idwt()

x -> n -> d -> t -> y
probe x as "clean"
probe n as "noisy"
probe y as "y"
