pipeline "renamed_time" v1
node x = gen(kind=sine, freq=440, rate=8000, duration=0.05)
node sp = speed(rate=1.25)
node ts = time_stretch(rate=1.25, method=vocoder, n_fft=512, hop=128)
node ps = pitch_shift(semitones=2, method=vocoder, n_fft=512, hop=128)
x -> sp
x -> ts
x -> ps
probe sp as "sp"
probe ts as "ts"
probe ps as "ps"
