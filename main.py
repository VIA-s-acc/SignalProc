import Signal as sg
import math
from math import sqrt
import random 
# Создаем сигнал
import sympy
import os
import numpy as np
from icecream import ic

current_directory = os.path.dirname(os.path.abspath(__file__))

wavfile_path = os.path.join(current_directory, 'champ1.wav')

mvlst = sg.wav_to_list(wavfile_path=wavfile_path)
mvdata = mvlst[0].tolist()
framerate = mvlst[1]

signal_values = [0,3,2,1,5] #степени во возрастанию  
signal = sg.Signal(signal_values)
x = sympy.Symbol("x")
x5 = sympy.Poly((1/x) + 1, 1/x, domain='QQ')**2 * sympy.Poly(2+sqrt(3)-(1/x), 1/x)
x5_1 = (-sqrt(2)/(4*(sqrt(3)-1))) *sympy.Poly((1+1/x),1/x, domain = ('QQ'))**2 * sympy.Poly(2-sqrt(3)-(1/x), 1/x)
polf = ((sqrt(3)-1)/(4*sqrt(2)), [(x5, 1), (x5_1, 1)])
poly = sympy.Poly('-1/16+9/16*x**(-2)+16/16*x**(-3)+9/16*x**(-4)-1/16*x**(-6)')
xd = sympy.Poly((1/x) + 1, 1/x, domain='QQ') * sympy.Poly(2+sqrt(3)-(1/x), 1/x)
xd_1 =  -(1/8) * sympy.Poly((1+1/x),1/x, domain = ('QQ'))**3 * sympy.Poly(2-sqrt(3)-(1/x), 1/x)
pold = ((1/2), [(xd, 1), (xd_1, 1)])

# Применяем функции
factorized_polynomial = signal.factorize_polynomial(Poly=poly)
factors = signal.filter_bank_6th_degree(factorized_polynomial)
factors.append(signal.generate_filters(polf))
factors.append(signal.generate_filters(pold))

h0, f0, h1, f1 = factors[0]

x = sg.Signal(mvdata, 0, sig_name='Начальный сигнал')

x.round(3)
result = x.recursive_analysis(x, max_depth=8, h0=h0, h1=h1)
for i in range(1,5):
    result[i].values = [0 for i in range(len(result[1].values))]

    
for i in range(len(result)):
    
    sg.list_to_wav(f"res00/4/result{i}.wav",result[i].values, framerate/((i+1)*2))


temp = x.recursive_synthesis(sig_list = result, f0=f0,f1=f1)
sg.list_to_wav("res00/4/result.wav", temp.values, framerate)

