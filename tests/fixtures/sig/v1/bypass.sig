pipeline "bypass" v1
node x = samples(values="1 -2 3 -4", rate=8)
node g = gain(db=-6) bypass
node h = gain(db=-6)
x -> g -> h
probe h as "h"
