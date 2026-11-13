pipeline "haar_bank" v1

node x   = samples(values="{{x}}", rate=8)
node d   = downsample(m=2, anti_alias=no, phase=1)
node u   = upsample(m=2, anti_image=no)
node sum = gain(db=0)

x -> d -> u -> sum
probe x as "__learn:sum"
