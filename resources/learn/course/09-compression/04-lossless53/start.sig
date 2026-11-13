pipeline "lossless53" v1

node x = samples(values="{{x}}", rate=32)
node l = lift53(levels=3, integer=no)
node i = ilift53()
node y = gain(db=0)

x -> l -> i -> y
probe y as "y"
