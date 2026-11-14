pipeline "first_steps" v1

node x       = gen(kind=doppler, rate=8000, duration=0.25)
node noisy   = add_noise(color=white, snr_db=10, seed=1)
node dwt     = dwt(wavelet=sym8, levels=0, ext=symmetric)
node thr     = threshold(rule=bayes, mode=soft, levels=1..64, sigma=mad_d1)
node idwt    = idwt()
node chk_in  = check(kind=snr, tol=0)
node chk_out = check(kind=snr, tol=0)

x -> noisy -> dwt -> thr -> idwt
x -> chk_in.ref
noisy -> chk_in.in
x -> chk_out.ref
idwt -> chk_out.in
probe noisy as "noisy"
probe idwt as "clean"
