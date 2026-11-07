pipeline "compression" v1

node x    = wav(path="../resources/samples/test.wav")
node dwt  = dwt(wavelet=db4, levels=5, ext=periodization)
node keep = keep_largest(mode=percent, amount=10, keep_approx=yes)
node idwt = idwt()
node snr  = check(kind=snr, tol=10, delay=0)

x -> dwt -> keep -> idwt -> snr.in
x -> snr.ref
probe keep as "10 % коэффициентов"
probe idwt as "x̂"
