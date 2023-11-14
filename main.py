import Signal as sg
import math
import random 
# Создаем сигнал
import sympy

signal_values = [0,3,2,1,5] #степени во возрастанию  
signal = sg.Signal(signal_values)
x = sympy.Symbol("x")
poly = sympy.Poly('-1/16+9/16*x**(-2)+16/16*x**(-3)+9/16*x**(-4)-1/16*x**(-6)')
# Применяем функции
factorized_polynomial = signal.factorize_polynomial(Poly=poly)
h0, f0, h1, f1 = signal.generate_filters(factorized_polynomial)
# print(H1)
# print(F1)
# H1P = sympy.Poly(H1.values, 1/x)
# F1P = sympy.Poly(F1.values, 1/x)

# print(H1P,'\n',F1P,'\n',H1P*F1P)


# sq = math.sqrt(2)
# h0 = sg.Signal([1/sq, 1/sq],0,1, sig_name = 'h0')
# h1 = sg.Signal([1/sq, -1/sq],0,1, sig_name = 'h1')

    
# f0 = sg.Signal([1/sq, 1/sq],-1,0, sig_name = 'f0')
# f1 = sg.Signal([-1/sq, 1/sq],-1,0, sig_name = 'f1')

lst1= [999,0,1,4,1,2,12,5,12,3,2,4,5,1,2,7,21,23,21,999]
print(lst1[:1])
x = sg.Signal(lst1, 0,5, sig_name='Начальный сигнал')

r0 = x.convolve(h0)
r1 = x.convolve(h1)
r0.set_name('r0')
r1.set_name('r1')
print(r0)
print(r1)
y0 = r0.downsample(2)
y1 = r1.downsample(2)
y0.set_name('y1')
y1.set_name('y1')
print(y0)
print(y1)
t0 = y0.upsample(2)
t1 = y1.upsample(2)
t0.set_name('t0')
t1.set_name('t1')
print(t0)
print(t1)
v0 = t0.convolve(f0)
v1 = t1.convolve(f1)
v0.set_name('v0')
v1.set_name('v1')
print(v0);
print(v1);



print(x)
xp = v0+v1
xp.set_name("Результирующий сигнал")
xp.round()
print(xp)



# test = Signal([1,0,2,0,1], -1, sig_name = 'test')
# test_ker = Signal([5],0,sig_name='test_ker')
# print(test.downsample(2))
