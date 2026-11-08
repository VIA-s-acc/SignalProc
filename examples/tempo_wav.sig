pipeline "tempo_wav" v1

# свой WAV: медленнее без смены тона, выше без смены темпа, быстрее как пластинка
node x    = wav(path="../resources/samples/test.wav")
node slow = time_stretch(rate=0.75, method=locked)
node up   = pitch_shift(semitones=3)
node rec  = speed(rate=1.25)

x -> slow
x -> up
x -> rec
probe slow as "медленнее"
probe up as "выше на 3"
probe rec as "пластинка"
