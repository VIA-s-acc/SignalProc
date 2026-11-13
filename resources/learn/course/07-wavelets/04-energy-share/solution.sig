pipeline "energy_share" v1

node x  = gen(kind=sine, freq={{f}}, amplitude=1, rate=256, duration=1)
node n  = add_noise(color=white, snr_db={{snr}}, seed={{seed}})
node w  = dwt(wavelet={{wv}}, levels=3, ext=periodization)
node be = band_energy()

x -> n -> w -> be
probe be as "energy"
