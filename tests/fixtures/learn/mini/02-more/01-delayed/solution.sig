pipeline "delayed" v2

node x = samples(values="{{x}}", rate=8)
node y = delay(samples=1)

x -> y
