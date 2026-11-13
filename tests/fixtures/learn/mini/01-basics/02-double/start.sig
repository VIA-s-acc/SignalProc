pipeline "double" v1

node x = samples(values="{{x}}", rate=8)
node y = gain(db=0)

x -> y
probe y as "y"
