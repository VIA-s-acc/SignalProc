pipeline "poly_conv" v1

node x  = samples(values="{{x}}", rate=8)
node p  = polyphase
node fe = fir_taps(taps="{{h0}} {{h2}}")
node dl = delay(samples=1)
node fo = fir_taps(taps="{{h1}} {{h3}}")
node y  = add()

x -> p
p.even -> fe -> y.a
p.odd -> dl -> fo -> y.b
probe y as "y"
