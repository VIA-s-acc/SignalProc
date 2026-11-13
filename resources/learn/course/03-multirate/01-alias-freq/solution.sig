pipeline "alias" v1

node s  = gen(kind=sine, freq={{f0}}, rate=9000, duration=4)
node d  = downsample(m={{m}}, anti_alias=no)
node pk = spectrum_peaks(count=1, segment=8192)

s -> d -> pk
probe d as "y"
