pipeline "renamed_analysis" v1
node x = gen(kind=harmonics, freq=220, rate=8000, duration=0.25)
node p = pitch(fmin=80, fmax=1000, frame=1024, hop=256)
node sb = sst_band(lo=100, hi=1000, voices=16)
node sr = sst_ridge(lo=100, hi=1000, voices=16)
node ld = loudness(window=0.05s, hop=0.025s)
node on = onsets(n=512, hop=256)
node ss = spectral_subtract(n=256)
node ok = assert(expr="a.windows > 0")
x -> p
x -> sb
x -> sr
x -> ld
x -> on
x -> ss.x
ld -> ok.a
probe p.report as "pitch"
probe p.f0 as "f0"
probe sb as "sb"
probe sr.report as "sr"
probe ld as "ld"
probe on as "on"
probe ss as "ss"
probe ok as "ok"
