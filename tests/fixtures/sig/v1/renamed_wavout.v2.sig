pipeline "renamed_wavout" v2

node x  = samples(values="0.1 0.2 0.3", rate=8)
node w  = wav_out(path="renamed_out.wav", bits=16, dither=yes, seed=1)
node w2 = wav_out(path="renamed_out2.wav", bits=16, dither=no)

x -> w
x -> w2
probe x as "x"
