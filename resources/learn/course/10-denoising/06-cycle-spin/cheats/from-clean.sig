pipeline "cycle_spinning" v1

node x  = gen(kind=bumps, rate=2048, duration=1)
node n  = add_noise(color=white, snr_db={{snr}}, seed={{seed}})
node y1 = denoise(wavelet=haar, levels=5, rule=bayes, mode=soft, shifts=1)
node d  = denoise(wavelet=haar, levels=5, rule=bayes, mode=soft, shifts=8)
node y  = gain(db=0)

x -> n -> y1
x -> d -> y
probe x as "clean"
probe n as "noisy"
probe y1 as "one shift"
probe y as "y"
