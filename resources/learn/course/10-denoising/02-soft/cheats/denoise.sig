pipeline "soft_threshold" v1

node x = gen(kind=doppler, rate=2048, duration=1)
node n = add_noise(color=white, snr_db={{snr}}, seed={{seed}})
node y = denoise(wavelet=sym8, levels=5, rule=bayes, mode=soft)

x -> n -> y
probe y as "y"
