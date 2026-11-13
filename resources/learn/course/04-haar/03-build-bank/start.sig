pipeline "haar_bank" v1

node x   = samples(values="{{x}}", rate=8)
node sum = gain(db=0)

x -> sum
probe sum as "x̂"
