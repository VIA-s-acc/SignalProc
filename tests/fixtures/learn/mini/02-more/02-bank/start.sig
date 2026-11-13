pipeline "bank" v1

node x   = samples(values="{{x}}", rate=8)
node sum = add()

x -> sum.a
x -> sum.b
