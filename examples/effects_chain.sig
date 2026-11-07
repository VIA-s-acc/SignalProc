pipeline "effects_chain" v1

node src  = chirp(method=log, f0=50, f1=4000, rate=16000, duration=0.5)
node env  = osc(kind=sawtooth, freq=3, rate=16000, duration=0.5)
node mod  = multiply(factor=0.5)
node lp   = iir(family=ellip, type=bandpass, order=4, cutoff="300, 3000", zero_phase=yes)
node fir  = fir(numtaps=63, cutoff=2000, compensate=yes)
node gain = multiply(factor=2)
node st   = merge()
node m    = stats()
node loud = loudness()
node dwt  = lift97(levels=3)
node d2   = mra(component=d2)

src -> mod.a
env -> mod.b
mod -> lp -> fir -> gain -> st.l
src -> st.r
st -> m
st -> loud
fir -> dwt -> d2
probe d2 as "d2"
