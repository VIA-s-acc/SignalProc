pipeline "renamed_time" v2

node x  = gen(kind=sine, freq=440, rate=8000, duration=0.05)
node sp = speed(factor=1.25)
node ts = time_stretch(factor=1.25, method=vocoder, frame=512, hop=128)
node ps = pitch_shift(semitones=2, method=vocoder, frame=512, hop=128)

x -> sp
x -> ts
x -> ps
probe sp as "sp"
probe ts as "ts"
probe ps as "ps"
