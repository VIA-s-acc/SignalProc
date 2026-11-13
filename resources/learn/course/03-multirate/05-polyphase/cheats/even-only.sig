pipeline "poly_conv" v1

node x  = samples(values="{{x}}", rate=8)
node p  = polyphase
node fe = fir_taps(taps="{{h0}} {{h2}}")
node y  = gain(db=0)

x -> p
p.even -> fe -> y
