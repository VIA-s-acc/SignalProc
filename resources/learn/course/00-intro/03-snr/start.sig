pipeline "snr" v1

node x   = gen(kind=sine, freq=440, amplitude={{s}}, rate=8000, duration=0.25)
node w   = gen(kind=white, amplitude={{a}}, rate=8000, duration=0.25, seed={{seed}})
node n   = add()
node chk = check(kind=snr, tol=0, delay=0)

x -> n.a
w -> n.b
x -> chk.ref
n -> chk.in
probe n as "noisy"
