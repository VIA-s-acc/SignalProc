pipeline "circulant" v1

node s0 = samples(values="1 1 1 1 1 1 1 1 1 1 1 1", rate=4)
node s2 = samples(values="1 -1 1 -1 1 -1 1 -1 1 -1 1 -1", rate=4)
node s1 = samples(values="1 0 -1 0 1 0 -1 0 1 0 -1 0", rate=4)
node f0 = fir_taps(taps="{{a}} {{b}} {{c}}")
node f2 = fir_taps(taps="{{a}} {{b}} {{c}}")
node f1 = fir_taps(taps="{{a}} {{b}} {{c}}")
node c0 = cut(begin=6)
node c2 = cut(begin=6)
node c1 = cut(begin=4, end=8)
node p0 = cut(begin=0, end=1)
node p2 = cut(begin=0, end=1)
node p1 = gain(db=0)

s0 -> f0 -> c0 -> p0
s2 -> f2 -> c2 -> p2
s1 -> f1 -> c1 -> p1
