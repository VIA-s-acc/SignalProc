pipeline "convolution" v1

node x = samples(values="{{x}}", rate=8)
node y = fir_taps(taps="{{h}}")

x -> y
probe y as "y"
