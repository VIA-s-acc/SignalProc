pipeline "soft_threshold" v1

node x = gen(kind=doppler, rate=2048, duration=1)
node n = add_noise(color=white, snr_db={{snr}}, seed={{seed}})
node y = gain(db=0)

x -> n -> y
probe x as "clean"
probe n as "noisy"
probe y as "y"
