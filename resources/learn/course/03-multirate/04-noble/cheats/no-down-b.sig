pipeline "noble" v1

node x  = samples(values="{{x}}", rate=8)
node ha = fir_taps(taps="{{h0}} 0 {{h1}} 0 {{h2}}")
node ya = downsample(m=2, anti_alias=no, phase=0)
node db = downsample(m=2, anti_alias=no, phase=0)
node yb = fir_taps(taps="{{h0}} {{h1}} {{h2}}")

x -> ha -> ya
x -> yb
