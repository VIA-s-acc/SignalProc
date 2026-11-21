pipeline "haar_blocks" v2

# Analysis: both branches, each filtered and downsampled by 2
block analysis(w=haar) {
    in x
    out a, d

    node h0 = convolve(filter=$w.h0, method=direct)
    node h1 = convolve(filter=$w.h1, method=direct)
    node da = downsample(m=2, anti_alias=no, phase=1)
    node dd = downsample(m=2, anti_alias=no, phase=1)

    x -> h0 -> da -> a
    x -> h1 -> dd -> d
}

# Synthesis: both bands upsampled, filtered and added
block synthesis(w=haar) {
    in a
    in d
    out y

    node ua = upsample(m=2, anti_image=no)
    node ud = upsample(m=2, anti_image=no)
    node f0 = convolve(filter=$w.f0, method=direct)
    node f1 = convolve(filter=$w.f1, method=direct)
    node s  = add()

    a -> ua -> f0 -> s.a
    d -> ud -> f1 -> s.b
    s -> y
}

node x   = samples(values="{{x}}", rate=16)
node an1 = analysis()
node an2 = analysis()
node sy2 = synthesis()
node y   = synthesis()

x -> an1
an1.a -> an2
an2.a -> sy2.a
an2.d -> sy2.d
sy2 -> y.a
an1.d -> y.d
probe y as "x̂"
