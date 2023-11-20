import Signal as sg
import math
from math import sqrt
import random 
import sympy
import os
import numpy as np
from icecream import ic
from art import tprint
from pathlib import Path
import datetime
import time
class colors:
    RESET = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'
    BLACK = '\033[30m'
    RED = '\033[31m'
    GREEN = '\033[32m'
    YELLOW = '\033[33m'
    BLUE = '\033[34m'
    MAGENTA = '\033[35m'
    CYAN = '\033[36m'
    WHITE = '\033[37m'
    BG_BLACK = '\033[40m'
    BG_RED = '\033[41m'
    BG_GREEN = '\033[42m'
    BG_YELLOW = '\033[43m'
    BG_BLUE = '\033[44m'
    BG_MAGENTA = '\033[45m'
    BG_CYAN = '\033[46m'
    BG_WHITE = '\033[47m'


def main():
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
    print(colors.YELLOW)
    tprint("[S] SIGNAL PROCESSING", font = "bulbhead")
    print(colors.RESET)
    fac_num = int(input("\n[I] CHOOSE FILTER NUM (FROM 0 TO 6): ")) or 0
    h0, f0, h1, f1 = factors[fac_num]
    if fac_num > 6 or fac_num < 0:
        raise ValueError("Filter_num error")
    mode_ = input("\n[I] INPUT MODE 'L','F' ('L' - list mode, 'F' - file mode): ") or "F"
    if mode_ == "L":
        len_ = int(input("\n[I] List Len: ")) or 1
        mvlst = []
        for i in len_:
            el = input(f'\n[I] {i} elem : ')
            if isinstance(el, (int,float)):
                mvlst.append(el)
            else:
                raise ValueError("[E] el must be int or float")
            
    elif mode_ == "F":
        wavfile_path = input("\n[I] input file path: ")
        if Path(wavfile_path).is_file() and Path(wavfile_path).suffix == '.wav':
            print(f"\n[+] Original File: {Path(wavfile_path).name}")
            print(f"\n[+] Processing...")
            start1_d = datetime.datetime.now()
            start1_t = time.time()
            mvlst = sg.wav_to_list(wavfile_path=wavfile_path)
            mvdata = mvlst[0].tolist()
            framerate = mvlst[1]
            print(f"\n[T] END wav_to_list:\nSTART: {start1_d}\nEND: {datetime.datetime.now()}\nExecution Time: {start1_t-time.time()} ")
        else:
            raise FileExistsError("\n[E] File not exists, check the file path.")

    else: 
        raise NameError("[E] Mode error")

    x = sg.Signal(mvdata, 0, sig_name='Начальный сигнал')
    r_mode = int(input("\n[I] Round_Mode int num: ")) or 1
    max_dep = int(input("\n[I] max depth input: ")) or 3
    x.round(r_mode)
    start1_d = datetime.datetime.now()
    start1_t = time.time()
    result = x.recursive_analysis(x, max_depth=max_dep, h0=h0, h1=h1)
    print(colors.GREEN)
    print(f"\n[T] END recursive_analysis:\nSTART: {start1_d}\nEND: {datetime.datetime.now()}\nExecution Time: {time.time()-start1_t} ")
    print(colors.RESET)
    if mode_ == "F":
        for i in range(len(result)):    
            start2_d = datetime.datetime.now()
            start2_t = time.time()
            sg.list_to_wav(f"result{i}.wav",result[i].values, framerate/((i+1)*2))
            print(colors.GREEN)
            print(f"\n[T] END list_to_wav{i}:\nSTART: {start2_d}\nEND: {datetime.datetime.now()}\nExecution Time: {time.time()-start2_t} ")
            print(colors.RESET)
    start1_d = datetime.datetime.now()
    start1_t = time.time()
    temp = x.recursive_synthesis(sig_list = result, f0=f0,f1=f1)
    
    print(colors.GREEN)
    print(f"\n[T] END recursive_analysis:\nSTART: {start1_d}\nEND: {datetime.datetime.now()}\nExecution Time: {time.time()-start1_t} ")
    print(colors.RESET)
    if mode_ == "F":
        for i in range(len(result)):    
            start2_d = datetime.datetime.now()
            start2_t = time.time()
            print(colors.GREEN)
            sg.list_to_wav(f"result.wav", temp.values, framerate)
            print(f"\n[T] END list_to_wav:\nSTART: {start2_d}\nEND: {datetime.datetime.now()}\nExecution Time: {time.time()-start2_t} ")
            print(colors.RESET)
    print(colors.YELLOW)
    tprint("[D] DONE", font = 'bulbhead')
    print(colors.RESET)
    return temp


if __name__ == "__main__":
    main()