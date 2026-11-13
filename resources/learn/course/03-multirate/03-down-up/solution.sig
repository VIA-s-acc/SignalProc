pipeline "down_up" v1

node x = samples(values="{{x}}", rate=8)
node d = downsample(m=2, anti_alias=no)
node u = upsample(m=2, anti_image=no)

x -> d -> u
probe u as "y"
