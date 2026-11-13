pipeline "soft_threshold" v1

node x = gen(kind=doppler, rate=2048, duration=1)
node n = add_noise(color=white, snr_db={{snr}}, seed={{seed}})
node d = dwt(wavelet=sym8, levels=5)
node t = threshold(rule=bayes, mode=soft)
node y = idwt()

x -> n -> d -> t -> y
probe x as "clean"
probe n as "noisy"
probe y as "y"
