pipeline "ellip_order" v1

node sp = gen(kind=sine, freq=1000, rate=8000, duration=1)
node ss = gen(kind=sine, freq=1500, rate=8000, duration=1)
node ep = iir(family=ellip, type=lowpass, order=4, cutoff=1000, rp=3, rs=40)
node es = iir(family=ellip, type=lowpass, order=4, cutoff=1000, rp=3, rs=40)
node fp = iir(family=ellip, type=lowpass, order=3, cutoff=1000, rp=3, rs=40)
node fs = iir(family=ellip, type=lowpass, order=3, cutoff=1000, rp=3, rs=40)

sp -> ep
ss -> es
sp -> fp
ss -> fs
probe ep as "edge, N"
probe es as "stop, N"
probe fp as "edge, N-1"
probe fs as "stop, N-1"
