pipeline "double" v1

node x = samples(values="{{x}}", rate=8)
node y = add()

x -> y.a
x -> y.b
probe y as "y"
