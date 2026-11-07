pipeline "audio_chain" v1

node x     = wav(path="../resources/samples/test.wav")
node hp    = biquad(type=highpass, f0=80, q=0.7071)
node bell  = biquad(type=peaking, f0=3000, q=1, gain_db=3)
node comp  = compressor(threshold_db=-18, ratio=3, attack_ms=10, release_ms=120, makeup_db=3)
node echo  = echo(delay_ms=250, feedback=0.3, mix=0.25)
node room  = reverb(room_size=0.6, damping=0.5, wet=0.25, dry=0.8)
node bands = band_power(bands=octave)
node peaks = spectrum_peaks(count=5)

x -> hp -> bell -> comp -> echo -> room
room -> bands
room -> peaks
probe comp as "после компрессора"
probe room as "результат"
probe bands as "октавы"
probe peaks as "пики"
