import json, sys, wave, numpy as np, pywt
from numpy.polynomial import polynomial as P
out = {}
w = wave.open(sys.argv[1], 'rb'); n = w.getnframes(); fr = w.getframerate()
x = np.frombuffer(w.readframes(n), dtype=np.int16).reshape(-1, 2).astype(float) / 32768
out['meta'] = dict(frames=n, rate=fr, dur=round(n/fr, 2), peakL=round(20*np.log10(np.abs(x[:,0]).max()),1), peakR=round(20*np.log10(np.abs(x[:,1]).max()),1),
                   rmsL=round(20*np.log10(np.sqrt((x[:,0]**2).mean())),1), rmsR=round(20*np.log10(np.sqrt((x[:,1]**2).mean())),1))
C = 192
def env(sig, cols):
    edges = np.linspace(0, len(sig), cols+1).astype(int)
    return [[int(round(sig[a:b].min()*100)), int(round(sig[a:b].max()*100))] for a, b in zip(edges[:-1], edges[1:])]
out['envL'] = env(x[:,0], C); out['envR'] = env(x[:,1], C)
mono = x.mean(1)
seg = 4096; hop = 2048; win = np.hanning(seg)
frames = np.lib.stride_tricks.sliding_window_view(mono, seg)[::hop] * win
S = (np.abs(np.fft.rfft(frames, axis=1))**2).mean(0)
f = np.fft.rfftfreq(seg, 1/fr)
fb = np.geomspace(20, fr/2, C+1)
spec = []
for a, b in zip(fb[:-1], fb[1:]):
    m = (f >= a) & (f < b)
    v = S[m].mean() if m.any() else S[np.argmin(abs(f-(a+b)/2))]
    spec.append(v)
spec = 10*np.log10(np.array(spec)/S.max())
out['spec'] = [int(round(max(v, -100))) for v in spec]
lev = 6; coeffs = pywt.wavedec(mono, 'db2', level=lev, mode='symmetric')
names = ['a6'] + [f'd{j}' for j in range(lev, 0, -1)]
tot = sum((c**2).sum() for c in coeffs)
bands = []
for nm, c in zip(names, coeffs):
    j = int(nm[1:])
    lo, hi = (0, fr/2**(j+1)) if nm[0]=='a' else (fr/2**(j+1), fr/2**j)
    cols = 96; edges = np.linspace(0, len(c), cols+1).astype(int)
    e = np.array([np.abs(c[a:b]).max() for a, b in zip(edges[:-1], edges[1:])])
    bands.append(dict(name=nm, len=len(c), lo=round(lo), hi=round(hi), energy=round(100*(c**2).sum()/tot, 2), env=[int(round(v)) for v in 100*e/e.max()]))
out['bands'] = bands
T, F = 96, 24
cw = pywt.Wavelet('db2')
hm = []
# scalogram-like map from SWT-free CWT on a decimated excerpt for speed
ex = mono[::4][:len(mono)//4]
scales = np.geomspace(2, 256, F)
coef, freqs = pywt.cwt(ex[:: max(1, len(ex)//(T*64)) ], scales, 'morl')
mag = np.abs(coef)
edges = np.linspace(0, mag.shape[1], T+1).astype(int)
mm = np.array([[mag[r, a:b].max() for a, b in zip(edges[:-1], edges[1:])] for r in range(F)])
mm = 20*np.log10(mm/mm.max() + 1e-9); mm = np.clip((mm + 48)/48, 0, 1)
out['scalogram'] = [''.join('0123456789abcdef'[int(v*15)] for v in row) for row in mm]
p = 3
y = np.array([1, 3, 6.0])
roots = []
for yr in np.roots(y[::-1]):
    a = 1 - 2*yr
    z1 = a + np.sqrt(a*a - 1 + 0j); z2 = a - np.sqrt(a*a - 1 + 0j)
    roots += [z1, z2]
out['p3roots'] = [[round(r.real, 4), round(r.imag, 4)] for r in roots]
db3 = pywt.Wavelet('db3')
out['db3'] = [round(v, 6) for v in db3.rec_lo]
h = np.array(db3.rec_lo); print("db3 roots of H0:", np.round(np.roots(h), 4))
xs = np.array([4, 6, 10, 12, 8, 6, 5, 5.0]); r2 = np.sqrt(2)
a1 = (xs[0::2] + xs[1::2])/r2; d1 = (xs[0::2] - xs[1::2])/r2
out['haar'] = dict(x=xs.tolist(), a=[round(v, 3) for v in a1], d=[round(v, 3) for v in d1])
print(json.dumps(out['meta']), [(b['name'], b['len'], b['lo'], b['hi'], b['energy']) for b in bands])
print("p3 roots", out['p3roots']); print("haar", out['haar'])
json.dump(out, open(sys.argv[2], 'w'), separators=(',', ':'))
