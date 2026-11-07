pipeline "lpc" v1

node x   = wav(path="../resources/samples/test.wav")
node lpc = lpc(order=16)
node syn = lpc_synth()
node pr  = check(kind=error, tol=1e-9, delay=0)

x -> lpc
lpc.residual -> syn.residual
lpc.report -> syn.report
syn -> pr.in
x -> pr.ref
probe lpc.residual as "остаток"
probe lpc.report as "коэффициенты"
