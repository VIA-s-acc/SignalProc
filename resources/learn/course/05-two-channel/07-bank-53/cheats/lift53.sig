pipeline "bank" v1

node x   = samples(values="{{x}}", rate=16)
node w   = lift53()
node i   = ilift53()
node sum = gain(db=0)

x -> w -> i -> sum
