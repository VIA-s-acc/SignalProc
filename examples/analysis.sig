pipeline "analysis" v1

node x     = gen(kind=harmonics, freq=220, rate=16000, duration=1)
node pitch = pitch(fmin=60, fmax=1000)
node mfcc  = mfcc(n_mfcc=13)
node feat  = spectral_features()
node peaks = spectrum_peaks(count=6)

x -> pitch
x -> mfcc
x -> feat
x -> peaks
probe pitch.report as "основной тон"
probe pitch.f0 as "f0 по кадрам"
probe mfcc as "MFCC"
probe feat as "признаки"
probe peaks as "пики"
