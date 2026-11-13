pipeline "convolution" v1

node x = samples(values="{{x}}", rate=8)
node p = pad(after=2)
node y = fir_taps(taps="{{h}}")

x -> p -> y
probe y as "y"
