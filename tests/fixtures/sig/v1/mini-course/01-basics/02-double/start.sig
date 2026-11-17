pipeline "double" v1

node x = samples(values="{{x}}", rate=8)
node m = multiply(factor=1)
node y = gain(db=0)

x -> y
probe y as "y"
x -> m.a
