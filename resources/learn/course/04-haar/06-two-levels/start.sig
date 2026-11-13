pipeline "haar_two_levels" v1

node x   = samples(values="{{x}}", rate=16)
node sum = gain(db=0)

x -> sum
probe sum as "x̂"
