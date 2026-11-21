pipeline "lowpass" v2

node t = gen(kind=sine, freq=1500, amplitude="{{a}}", rate=8000, duration=0.5)
node x = dc_offset(offset="{{c}}")
node y = fir(numtaps=101, cutoff=300)

t -> x -> y
probe x as "x"
probe y as "y"
