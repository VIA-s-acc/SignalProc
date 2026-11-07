pipeline "packets" v1

node x    = wav(path="../resources/samples/test.wav")
node wpt  = wpt(wavelet=db4, levels=4, basis=best, ext=periodization)
node iwpt = iwpt()
node pr   = check(kind=pr, tol=1e-9, delay=0)

x -> wpt -> iwpt -> pr.in
x -> pr.ref
probe wpt as "лучший базис"
