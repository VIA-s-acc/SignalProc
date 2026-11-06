pipeline "denoise" v1

node src  = wav(path="../resources/samples/test.wav")
node dwt  = dwt(wavelet=sym8, levels=0, ext=symmetric)
node thr  = threshold(rule=bayes, mode=soft, levels=1..64, sigma=mad_d1)
node idwt = idwt()
node chk  = check(kind=snr, tol=0)
node out  = wav_out(path="denoised.wav", bits=24, dither=none)

src -> dwt -> thr -> idwt -> out
src -> chk.ref
idwt -> chk.in
probe dwt.d1 as "d1"
