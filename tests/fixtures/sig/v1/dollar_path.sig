pipeline "dollar" v1
node x = wav(path="tone$1.wav")
node g = gain(db=-6)
x -> g
probe g as "g"
