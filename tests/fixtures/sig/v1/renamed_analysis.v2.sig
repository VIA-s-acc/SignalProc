pipeline "renamed_analysis" v2

node x  = gen(kind=harmonics, freq=220, rate=8000, duration=0.25)
node p  = pitch(min_hz=80, max_hz=1000, frame=1024, hop=256)
node sb = sst_band(min_hz=100, max_hz=1000, voices=16)
node sr = sst_ridge(min_hz=100, max_hz=1000, voices=16)
node ld = loudness(frame=0.05s, hop=0.025s)
node on = onsets(frame=512, hop=256)
node ss = spectral_subtract(frame=256)
node ok = assert(expr="a.frames > 0")

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
