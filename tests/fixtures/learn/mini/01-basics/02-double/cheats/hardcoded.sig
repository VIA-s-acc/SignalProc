pipeline "double" v1

node x = samples(values="{{x}}", rate=8)
node z = samples(values="{{x}}", rate=8)
node y = add()

z -> y.a
z -> y.b
probe y as "y"
