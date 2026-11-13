pipeline "hard_soft" v1

node x   = gen(kind=bumps, rate=2048, duration=1)
node n   = add_noise(color=white, snr_db={{snr}}, seed={{seed}})
node d   = dwt(wavelet=sym8, levels=5)
node t   = threshold(rule=visu, mode=soft)
node y   = idwt()
node chk = check(kind=snr, tol=0, delay=0)

x -> n -> d -> t -> y
x -> chk.ref
y -> chk.in
probe n as "noisy"
probe y as "y"
