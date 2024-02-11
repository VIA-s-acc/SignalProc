# Signal Processing Library

[Features](#Features) | [Installation](#Installation) | [Usage](#Usage) | [Dependencies](#Dependencies) | [Contributing](#Contributing) | [License](#License)

This is a Python library for signal processing tasks, including analysis, synthesis, and various signal transformations. The library provides functionality for reading and writing WAV files, signal analysis through convolution and downsampling, recursive analysis and synthesis methods, and more.

[Changelog](#(https://github.com/VIA-s-acc/SignalProc/blob/main/CHANGELOG.md))
[doc](https://viag.pythonanywhere.com/article/65)

## Features

- Signal class for working with signals
- Functions for memoization and signal processing
- Conversion functions for audio data between WAV files and lists
- Filter bank implementation for polynomial signal processing
- Efficient matrix-based convolution algorithm
- Color-coding class for terminal output

## Installation

To use this library, ensure you have Python 3.10 installed. You can install the library and its dependencies using pip:

## Usage

```python
from signal_processing import Signal

# Example usage
signal = Signal()
# Perform signal analysis
signal.recursive_analysis()
# Perform signal synthesis
signal.recursive_synthesis()

## Dependencies
Python 3.10
art 6.1
asttokens 2.4.1
colorama 0.4.6
executing 2.0.1
future 0.18.3
icecream 2.1.3
lxml 4.9.3
mpmath 1.3.0
numpy 1.26.2
Pygments 2.16.1
six 1.16.0
sympy 1.12
Wave 0.0.2

##Contributing

Contributions are welcome! Feel free to open an issue or submit a pull request.

## License
This project is licensed under the MIT License - see the LICENSE file for details.
