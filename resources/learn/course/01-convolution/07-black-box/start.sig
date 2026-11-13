pipeline "black_box" v1

node x = samples(values="3 -1 4 1 -5 2 0 0", rate=8)
node t = fir_taps(taps="1 {{a}}")
node dl = delay(samples={{d}})
node m = multiply(factor={{k}})
node s = add()
node y = cut(begin=0, end=6)

x -> t -> s.a
x -> dl -> m -> s.b
s -> y
probe y as "y"
