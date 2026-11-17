pipeline "double" v1

node x = samples(values="{{x}}", rate=8)
node m = multiply(factor=1)
node y = add()

x -> y.a
x -> y.b
probe y as "y"
x -> m.a
