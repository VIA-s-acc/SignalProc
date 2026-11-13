pipeline "poly_conv" v1

node x  = samples(values="{{x}}", rate=8)
node f  = fir_taps(taps="{{h0}} {{h1}} {{h2}} {{h3}}")
node d  = downsample(m=2, anti_alias=no)
node y  = gain(db=0)
node p  = polyphase

x -> f -> d -> y
x -> p
