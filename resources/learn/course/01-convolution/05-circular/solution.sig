pipeline "circular" v1

node x = samples(values="{{x}}", rate=8)
node p = pad(after=2)
node f = fir_taps(taps="{{h}}")
node head = cut(begin=0, end=4)
node tail = cut(begin=4)
node tp = pad(after=2)
node y = add()

x -> p -> f
f -> head -> y.a
f -> tail -> tp -> y.b
probe y as "y"
