pipeline "tempo" v1

# пила 220 Гц тремя способами: растяжение не меняет тон, сдвиг высоты не меняет длину, скорость меняет и то и другое
node x      = osc(kind=sawtooth, freq=220, duration=2, rate=22050)
node slow   = time_stretch(rate=0.5, method=locked)
node up     = pitch_shift(semitones=12)
node rec    = speed(rate=2)
node f_x    = pitch()
node f_slow = pitch()
node f_up   = pitch()
node f_rec  = pitch()

x -> slow -> f_slow
x -> up -> f_up
x -> rec -> f_rec
x -> f_x
probe slow as "вдвое медленнее"
probe up as "октава вверх"
probe rec as "пластинка ×2"
probe f_x.report as "тон x"
probe f_slow.report as "тон slow"
probe f_up.report as "тон up"
probe f_rec.report as "тон rec"
