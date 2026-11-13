pipeline "hard_soft" v1

node x   = gen(kind=bumps, rate=2048, duration=1)
node n   = add_noise(color=white, snr_db={{snr}}, seed={{seed}})
node d   = dwt(wavelet=sym8, levels=5)
node th  = threshold(rule=visu, mode=hard)
node ts  = threshold(rule=visu, mode=soft)
node yh  = idwt()
node ys  = idwt()

x -> n -> d
d -> th -> yh
d -> ts -> ys
probe yh as "hard"
probe ys as "soft"
