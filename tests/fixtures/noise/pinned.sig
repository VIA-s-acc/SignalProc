pipeline "pinned" v2
node w = gen(kind=white, rate=8000, duration=0.001, seed=1)
node d = quantize(bits=8, dither=yes, seed=1)
w -> d
probe w as "w"
probe d as "d"
