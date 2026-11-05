import Signal as sg
from Signal import colors
from math import sqrt
import sympy
import os
from art import tprint
from pathlib import Path
import datetime
import time




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
    fac_num = input("\n[I] CHOOSE FILTER NUM (FROM 0 TO 6): ")
    if not fac_num.isdigit():
        fac_num = 0
    else:
        fac_num = int(fac_num)
    h0, f0, h1, f1 = factors[fac_num]
    if fac_num > 6 or fac_num < 0:
        raise IndexError("FILTER_NUM error")
    mode_ = input("\n[I] INPUT MODE 'L','F' ('L' - list mode, 'F' - file mode): ") or "F"
    if mode_ == "L":
        len_ = input("\n[I] List Len: ")
        if not len_.isdigit():
            len_ = 1
        mvlst = []
        for i in len_:
            el = input(f'\n[I] {i} elem : ')
            if isinstance(el, (int,float)):
                mvlst.append(el)
            else:
                raise ValueError("[E] ELEMENT TYPE MUST BE INT OR FLOAT")
            
    elif mode_ == "F":
        wavfile_path = input("\n[I] INPUT FILE PATH: ")
        if Path(wavfile_path).is_file() and Path(wavfile_path).suffix == '.wav':
            print(f"\n[+] ORIGINAL FILE: {Path(wavfile_path).name}")
            print(f"\n[+] PROCESSING...")
            start1_d = datetime.datetime.now()
            start1_t = time.time()
            mvlst = sg.wav_to_list(wavfile_path=wavfile_path)
            mvdata = mvlst[0].tolist()
            framerate = mvlst[1]
            print(colors.GREEN)
            print(f"\n[T] END wav_to_list:\nSTART: {start1_d}\nEND: {datetime.datetime.now()}\nEXECUTION TIME: {time.time()-start1_t} ")
            print(colors.RESET)
        else:
            raise FileExistsError("\n[E] FILE NOT EXISTS, CHECK THE FILE PATH.")

    else: 
        raise NameError("[E] Mode error")

    x = sg.Signal(mvdata, 0, sig_name='Начальный сигнал')
    r_mode = input("\n[I] ROUND DIGITS NUM: ")
    if not r_mode.isdigit():
        r_mode = 0
    else:
        r_mode = int(r_mode)
    max_dep = input("\n[I] MAX DEPTH INPUT: ")
    if not max_dep.isdigit():
        max_dep = 3
    else:
        max_dep = int(max_dep)
    x.round(r_mode)
    start1_d = datetime.datetime.now()
    start1_t = time.time()
    result = x.recursive_analysis(x, max_depth=max_dep, h0=h0, h1=h1)
    print(colors.GREEN)
    print(f"\n[T] END recursive_analysis:\nSTART: {start1_d}\nEND: {datetime.datetime.now()}\nExecution Time: {time.time()-start1_t} ")
    print(colors.RESET)
    if mode_ == "F":
        del_mode_ = input(f"\n[I] DO YOU WANT TO SET SOME COMPONENTS TO 0? [VALID RANGE FROM 1 TO {max_dep}] (Y|N): ") or 'n'
        if del_mode_.lower() == "y":
            del_range_1 = int(input("\n[I] RANGE FIRST BOARD: ")) 
            del_range_2 = int(input("\n[I] RANGE SECOND BOARD: "))
            if del_range_2 < del_range_1:
                raise ValueError("\n[E] SECOND BOARD MUST BE >= FIRST BOARD.")
            elif del_range_1 < 1 or del_range_2 < 1:
                raise ValueError('\n[E] RANGE BOARDS MUST BE >= 1')
            elif del_range_2 > max_dep:
                raise ValueError("\n[E] FIRST BOARD MUST BE >= MAX DEPTH.")
            for i in range(del_range_1, del_range_2+1):
                result[i].values = [0 for i in range(len(result[i].values))]
            print(f"\n[+] COMPONENTS FROM {del_range_1} TO {del_range_2} SET TO 0.")            
        elif del_mode_.lower() == "n":
            pass
        else:
            raise ValueError("\n[E] Y or N")
        path_ = f'Result_{datetime.datetime.now().strftime("%Y%m%d%H%M%S")}'
        try:
            os.mkdir(path_)
        except FileExistsError:
            raise FileExistsError("\n[E] RESULT FOLDER CREATING ERROR | FOLDER ALREADY EXISTS.")
        except Exception as e:
            raise ValueError("\n[E] RESULT FOLDER CREATING ERROR.")
        for i in range(len(result)):    
            
            start2_d = datetime.datetime.now()
            start2_t = time.time()
            sg.list_to_wav(f"{path_}/result{i}.wav",result[i].values, framerate/((i+1)*2))
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
            sg.list_to_wav(f"{path_}/result.wav", temp.values, framerate)
            print(colors.GREEN)
            print(f"\n[T] END list_to_wav:\nSTART: {start2_d}\nEND: {datetime.datetime.now()}\nExecution Time: {time.time()-start2_t} ")
            print(colors.RESET)
    print(colors.GREEN)
    print(f"[D] RESULTS SAVED IN {Path(path_)}")
    print(colors.RESET)
    print(colors.YELLOW)
    tprint("[D] DONE", font = 'bulbhead')
    print(colors.RESET)
    return temp
