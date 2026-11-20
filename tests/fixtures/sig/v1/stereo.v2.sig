pipeline "stereo" v2

node x = samples(values="1 2 3 4; -1 -2 -3 -4", rate=8)
node g = gain(db=-6)

x -> g
probe g as "g"
