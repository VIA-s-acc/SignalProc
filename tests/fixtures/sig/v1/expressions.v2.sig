pipeline "expr" v2

node x = samples(values="1 2 3 4", rate=8)
node g = gain(db="20*log10(2)")
node c = cut(begin=1, end=0.375s)

x -> g -> c
probe c as "c"
