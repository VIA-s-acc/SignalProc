import sympy
from icecream import ic
import time 
import os
import wave
import numpy as np
import copy
DEBUG = False
if DEBUG != True:
    ic.disable()


# Эта функция memoize представляет собой декоратор, который добавляет мемоизацию (кэширование) к функции, которую вы передаете в качестве аргумента.
# Мемоизация - это техника оптимизации, при которой результаты выполнения функции сохраняются, чтобы избежать повторных вычислений при одинаковых входных аргументах.

# Давайте рассмотрим пошагово, как это работает:

# make_immutable(obj): Эта вспомогательная функция используется для 
# рекурсивного преобразования изменяемых объектов в неизменяемые типы. 
# Это важно для того, чтобы объекты могли быть использованы в качестве ключей в словаре cache.

# **wrapper(*args, kwargs): Это оберточная функция, которая 
# заменяет оригинальную функцию. Она создает ключ key из входных аргументов 
# args и kwargs, где аргументы преобразуются в неизменяемые объекты с использованием make_immutable.

# Если ключ key отсутствует в кэше, выполняется оригинальная 
# функция (func(*args, **kwargs)), и результат сохраняется в кэше под ключом key. 
# После этого значение возвращается.

# Если ключ key уже есть в кэше, функция просто возвращает сохраненное значение, 
# избегая повторного выполнения функции для тех же входных аргументов.

# Время выполнения функции замеряется с использованием time.time(), и если переменная recursion_depth равна 0, то 
# есть функция не вызывается из других функций (т.е. это самый верхний уровень рекурсии), выводится общее время выполнения функции.
def memoize(func):
    """Memoize decorator to cache results of function calls.

    This decorator is used to store and retrieve the results of function calls
    based on their input arguments. It helps in optimizing functions by avoiding
    redundant calculations for the same input arguments.

    Args:
        func (callable): The function to be memoized.

    Returns:
        callable: A wrapped function that caches and returns results based on
                  input arguments.
    """
    cache = {}
    recursion_depth = 0  # Track recursion depth

    def make_immutable(obj):
        """Recursively convert mutable objects to immutable types."""
        if isinstance(obj, list):
            return tuple(map(make_immutable, obj))
        elif isinstance(obj, dict):
            return frozenset((key, make_immutable(value)) for key, value in obj.items())
        else:
            return obj

    def wrapper(*args, **kwargs):
        """Wrapper function for memoization.

        Args:
            *args: Variable-length argument list.
            **kwargs: Variable-length keyword argument list.

        Returns:
            Any: Result of the original function for the given arguments.
        """
        start_time = time.time()
        nonlocal recursion_depth
        key = (make_immutable(args), frozenset((key, make_immutable(value)) for key, value in kwargs.items()))
        if key not in cache:
            recursion_depth += 1
            cache[key] = func(*args, **kwargs)           
            recursion_depth -= 1

        end_time = time.time()
        if recursion_depth == 0:
            execution_time = end_time - start_time
            ic(f"Function: {func.__name__}, Args: {args}, Kwargs: {kwargs}, Execution Time: {execution_time:.6f} seconds")
        return cache[key]
    return wrapper

class Signal:
    """
    A class for working with signals.

    Args:
        values (list): A list of values representing the signal.
        start_index (int, optional): The starting index. Defaults to 0.
        end_index (int, optional): The ending index. Defaults to None.
        sig_name (str, optional): The name of the signal. Defaults to None.

    Raises:
        ValueError: If `end_index` is invalid.
        ValueError: If `sig_name` is not a valid string.

    Attributes:
        values (list): List of values representing the signal.
        start_index (int): The starting index of the signal.
        end_index (int): The ending index of the signal.
        name (str): The name of the signal.
        length (int): Length of the signal.

    Methods:
        downsample(factor):
            Downsample the signal by a given factor.

            Args:
                factor (int): The downsampling factor.

            Raises:
                ValueError: If the factor is less than or equal to 0.

            Returns:
                Signal: A downsampled signal.

        upsample(factor):
            Upsample the signal by a given factor.

            Args:
                factor (int): The upsampling factor.

            Raises:
                ValueError: If the factor is less than or equal to 0.

            Returns:
                Signal: An upsampled signal.

        convolve(kernel):
            Convolve the signal with another signal (kernel).

            Args:
                kernel (Signal): The kernel signal for convolution.

            Raises:
                TypeError: If the kernel is not a Signal instance.

            Returns:
                Signal: The convolved signal.

        round(ndigits=0):
            Round the signal values to a specified number of decimal places.

            Args:
                ndigits (int, optional): Number of decimal places. Defaults to 0.

            Raises:
                ValueError: If `ndigits` is negative.

        __add__(other):
            Add two Signal objects element-wise.

            Args:
                other (Signal): Another Signal object to add.

            Returns:
                Signal: The result of element-wise addition.

        __str__():
            Convert the Signal object to a string representation.

            Returns:
                str: A string containing signal information.
  
        __len__():
    
            Get the length of the Signal.

            Returns:
                int: The length of the Signal, which is the number of elements it contains.
    """
    def __init__(self, values, start_index:int = 0, end_index = None, sig_name = None):
        """
        Initialize a Signal object.

        Example:
        ```
        signal = Signal([1, 2, 3, 4])
        ```

        This class allows you to work with signals, including downsampling, upsampling, convolution, and more.
        """
        self.values = values
        self.start_index = start_index
        if end_index == None:
            self.end_index = start_index + len(values) - 1
        elif isinstance(end_index, int) and end_index > start_index and end_index < len(values):
            self.end_index = end_index
        else:
            raise ValueError("end_index_err")
        if sig_name == None:
            self.name = (f"<Sigobj{hex(id(self))}>")
        elif isinstance(sig_name, str):
            self.name = sig_name
        else:
            raise ValueError("sig_name_err")
        self.length = self.end_index - start_index + 1
        ic()

    def downsample(self, factor):
        """
        Downsample the signal by a given factor.

        Example:
        ```
        downsampled_signal = signal.downsample(2)
        ```

        Args:
            factor (int): The downsampling factor.

        Returns:
            Signal: A downsampled signal.
        """
        if factor <= 0:
            raise ValueError("Compression factor should be greater than 0")
        compressed_values = [self.values[i] for i in range(0, self.length, factor)]
        ic()
        return Signal(compressed_values, self.start_index)
        

    def upsample(self, factor) -> 'Signal':
        """
        Upsample the signal by a given factor.

        Example:
        ```
        upsampled_signal = signal.upsample(3)
        ```

        Args:
            factor (int): The upsampling factor.

        Returns:
            Signal: An upsampled signal.
        """
        if factor <= 0:
            raise ValueError("Stretch factor should be greater than 0")
        stretched_values = []
        for i in range(self.length):
            stretched_values.append(self.values[i])
            if i != self.length-1: 
                stretched_values.extend([0] * (factor - 1))  # Insert zeros
        ic()
        return Signal(stretched_values, self.start_index)



    def _old_convolve(self, kernel) -> 'Signal':
        """
            OLD VERSION WARNING

            Convolve the signal with another signal (kernel).

            Example:
            ```
            convolved_signal = signal.convolve(kernel_signal)
            ```

            Args:
                kernel (Signal): The kernel signal for convolution.

            Returns:
                Signal: The convolved signal.
        """
        if not isinstance(kernel, Signal):
            raise TypeError("The kernel should be an instance of the Signal class")
        convolve_start_index = self.start_index + kernel.start_index
        result_length = self.length + kernel.length - 1
        result_values = [0] * (result_length)
        X_set = [0]*(result_length-self.length)
        Y_set = kernel.values
        Y_set.reverse()
        X_set.extend(self.values[:self.end_index+1])
        X_set.extend([0]*(result_length-self.length))
        for i in range(result_length):
            Z_set = [0] * (2*result_length-self.length)
            for j,k in zip(range(i, i+kernel.length), range(len(Y_set))):
                Z_set[j] =  Y_set[k]
            for l in range(len(Z_set)):
                result_values[i] += Z_set[l]*X_set[l]
        ic()
        return Signal(result_values, convolve_start_index)

    def convolve(self, kernel) -> 'Signal':
        """
            Convolve the signal with another signal (kernel).

            Example:
            ```
            convolved_signal = signal.convolve(kernel_signal)
            ```

            Args:
                kernel (Signal): The kernel signal for convolution.

            Returns:
                Signal: The convolved signal.
        """
        matrix = []
        result_list = [] 
        convolve_start_index = self.start_index + kernel.start_index
        result_length = self.length + kernel.length - 1
        result_values = [0] * (result_length)
        for val in kernel:
            matrix.append([val*item for item in self])
        rows, cols = len(matrix), len(matrix[0])

        for sum_indices in range(rows + cols - 1):
            for i in range(max(0, sum_indices - cols + 1), min(sum_indices + 1, rows)):
                j = sum_indices - i
                result_values[sum_indices] += matrix[i][j]
        ic()
        
        return Signal(result_values, convolve_start_index)


    def Analysis(self,h0,h1):
        """
            Perform analysis of the signal by convolving it
            with two given signals (h0 and h1). The result is downsampled
            by a factor of 2, and the intermediate signals are named and returned.

        Args:
            h0: Signal for convolution.
            h1: Signal for convolution.
        Returns:

            Tuple of two downsampled signals (y0, y1).
        """
        r0 = self.convolve(h0)
        r1 = self.convolve(h1)
        y0 = r0.downsample(2)
        y1 = r1.downsample(2)
        ic()
        return y0, y1
    
    @memoize
    def recursive_analysis(self, x, h0, h1, max_depth=10,result = []):
        """
        Recursively perform analysis of the signal by applying the
         Analysis method. This is useful for multi-level signal processing.

        Args:

            x: Signal object to be recursively analyzed.
            h0: Signal for convolution.
            h1: Signal for convolution.
            depth: Current depth in recursion (default is 0).
            max_depth: Maximum recursion depth (default is 10).
            result: List to store results.
        Returns:

            List of recursively analyzed signals.
        """
        sig = copy.deepcopy(x)
        for i in range(max_depth):
            y0, y1 = sig.Analysis(h0,h1)
            result.append(y1)
            sig = y0
        result.append(y0)
        return result
    
    @memoize
    def recursive_synthesis(self,sig_list,f0, f1):
        """
        Recursively synthesize signals from a list using the 
        Synthesis method until a single signal is obtained.

        Args:

            sig_list: List of signals to be synthesized.
            f0: Signal for convolution.
            f1: Signal for convolution.
        Returns:

            The synthesized signal.
        """
        iter_list = copy.deepcopy(sig_list)
        while len(iter_list) != 1:
            y0 = iter_list[len(iter_list)-1]
            y1 = iter_list[len(iter_list)-2]
            result = self.Synthesis(y0,y1,f0,f1)
            iter_list.pop()
            iter_list[len(iter_list)-1] = result
        return iter_list[0]
    
    def Synthesis(self, y0,y1,f0,f1):
        """
        Recursively synthesize signals from a list using 
        the Synthesis method until a single signal is obtained.

        Args:

            y0,y1: signals to be synthesized.
            f0: Signal for convolution.
            f1: Signal for convolution.
        Returns:

            The synthesized signal.
        """

        t0 = y0.upsample(2)
        t1 = y1.upsample(2)
        v0 = t0.convolve(f0)
        v1 = t1.convolve(f1)
        start_index = min(v0.start_index, v1.start_index)
        v0 = Signal(v0.values[3:-2],v0.start_index)
        v1 = Signal(v1.values[3:-2],v0.start_index)
        ic()
        return v0+v1


    def factorize_polynomial(self, Poly:sympy.Poly=None):
        """
        Factorize the signal values as a polynomial.

        Example:
        ```
        signal.factorize_polynomial()
        ```

        Returns:
            str: The factorized polynomial.
        """
        if Poly is None:
            x = sympy.symbols('x')
            polynomial = sum([value * x**(index+self.start_index) for index, value in enumerate(self.values)])
        else:
            polynomial = Poly
        factorized_polynomial = sympy.Poly(polynomial).factor_list()
        ic()
        return factorized_polynomial

    def generate_filters(self,HF):
        """
        Generate filters H1, F1 using H0, F0.

        Example:
        ```
        H0, F0, H1, F1 = signal.generate_filters(HF)
        ```

        Returns:
            Signal: Filter H0.
            Signal: Filter F0.        
            Signal: Filter H1.
            Signal: Filter F1.
        """
        if len(HF[1]) != 2:
            raise ValueError("Need H0 and F0")
        c1=1
        c2=1
        try:
            HF[0]
            c1=HF[0]
        except:
            pass
        try:
            HF[2]
            c2=HF[2]
        except:
            pass
        H0 = Signal([c1 * coef for coef in (HF[1][0][0]**HF[1][0][1]).coeffs()],sig_name="H0")
        F0 = Signal([c2 * coef for coef in (HF[1][1][0]**HF[1][1][1]).coeffs()],sig_name="F0") 
        F1 = Signal([coef * (-1)**(n+1) for coef, n in zip(H0.values, range(len(H0.values)))], sig_name = 'F1')
        H1 = Signal([coef * (-1)**(n) for coef, n in zip(F0.values, range(len(F0.values)))], sig_name = "H1")
        ic()
        return H0,F0,H1,F1

    def filter_bank_6th_degree(self, HF):
        """
        Implement a filter bank for a polynomial of 6th degree.

        Example:
        ```
        signal.filter_bank_6th_degree()
        ```

        Returns:
            list: A list of six filtered signals.
        """

        rest = copy.deepcopy(HF)
        result = []
        for i in range(HF[1][0][1]):
            result.append(self.generate_filters(HF))
            temp = (HF[1][0][0],HF[1][0][1]-1)
            HF[1][0] = temp
            temp1 = (HF[1][1][0] * HF[1][0][0],1)
            HF[1][1] = temp1
            
            if HF[1][0][1] == 0:
                result.append(self.generate_filters(HF))
                break
        HF = rest
        return result
 
    
 

    
    def round(self, ndigits:int=0):
        """
        Round the signal values to a specified number of decimal places.

        Example:
        ```
        signal.round(2)
        ```

        Args:
            ndigits (int, optional): Number of decimal places. Defaults to 0.
        """
        ic()
        if ndigits>=0:
            if ndigits == 0: self.values = [round(value) for value in self.values] 
            else: self.values = [round(value,ndigits) for value in self.values]
        else:
            raise ValueError('ndigits_err')
    


    def __add__(self, other:"Signal") -> 'Signal':
        """
        Add two Signal objects element-wise.

        Example:
        ```
        result_signal = signal1 + signal2
        ```

        Args:
            other (Signal): Another Signal object to add.

        Returns:
            Signal: The result of element-wise addition.
        """
        ic()
        
        return Signal([x+y for x,y in zip(self.values,other.values)], self.start_index)

    def __len__(self):
        """
        Get the length of the Signal.

        Returns:
            int: The length of the Signal, which is the number of elements it contains.
        """
        ic()
        return len(self.values)

    def __str__(self):
        """
        Convert the Signal object to a string representation.

        Example:
        ```
        print(signal)
        ```

        Returns:
            str: A string containing signal information.
        """
        ic()
        return (
            f'Name: {self.name}\n'
            f"Signal: {self.values}\n"
            f"Start Index: {self.start_index}\n"
            f"Finish Index: {self.end_index}\n"
            f"Signal Length: {self.length}\n"
        )

    def __getitem__(self,key):
        """
        Get item from Signal.values by Key
        """
        ic()
        return self.values[key]
    
    def __iter__(self):
        """Return an iterator over the elements in the vector.

        Returns:
            iterator: An iterator over the elements in the vector.
        """
        ic()
        return iter(self.values)
    

    def set_name(self,name:str):
        """Set Signal Name
        Example:
        ```
        signal.set_name('Example_name')
        ```
        Args:
            name (str): name of signal to set
        """
        ic()
        self.name = name;

def wav_to_list(wavfile_path):
    """Converts a WAV file to a list of audio data.

    Args:
        wavfile_path (str): The file path to the input WAV file.

    Returns:
        tuple: A tuple containing the following elements:
            - numpy.ndarray: An array containing the audio data.
            - int: The number of frames in the audio file.
            - int: The frame rate of the audio file.
    """
    wav_data = []

    # Открываем WAV-файл
    with wave.open(wavfile_path, 'rb') as wavfile:
        # Получаем параметры аудио
        framerate = wavfile.getframerate()
        nframes = wavfile.getnframes()

        # Читаем аудиоданные и добавляем их в список
        frames = wavfile.readframes(nframes)
        wav_data = np.frombuffer(frames, dtype=np.int16)

    return wav_data, framerate


def list_to_wav(wavfile_path, wav_data, framerate):
    """Saves audio data to a WAV file.

    Args:
        file_path (str): The file path to save the output WAV file.
        wav_data (list): An array containing the audio data.
        framerate (int): The frame rate of the audio data.
    """

    # Открываем WAV-файл для записи
    with wave.open(wavfile_path, 'wb') as wavfile:
        # Устанавливаем параметры аудио
        wavfile.setnchannels(1)  # Монофонический звук
        wavfile.setsampwidth(2)  # 16 бит на отсчет
        wavfile.setframerate(framerate*2)

        # Преобразуем данные в байты и записываем их в WAV-файл
        wavfile.writeframes(np.int16(wav_data).tobytes())