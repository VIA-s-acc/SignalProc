# Создаем сигнал
import Signal as sg
import sympy

signal_values = [0,3,2,1,5] #степени во возрастанию  
signal = sg.Signal(signal_values)
x = sympy.Symbol("x")
poly = sympy.Poly('-1/16+9/16*x**(-2)+16/16*x**(-3)+9/16*x**(-4)-1/16*x**(-6)')
# Применяем функции
factorized_polynomial = signal.factorize_polynomial(Poly=poly)
H1,F1 = signal.generate_filters(factorized_polynomial)
print(H1)
print(F1)
H1P = sympy.Poly(H1.values, 1/x)
F1P = sympy.Poly(F1.values, 1/x)

print(H1P,'\n',F1P,'\n',H1P*F1P)
