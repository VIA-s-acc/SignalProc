# Changelog
## [Unreleased]

## [0.1.1] 2023-07-13

### Changed

- *To optimize the code*, recursion was removed in the `recursive_analysis` and `recursive_synthesis` functions (*the names of the functions have not been changed yet, but now they work cyclically*).

### Fixed

- The `memoization decorator` *is disabled*.
- Fixed a typo in the round method documentation.
- Fixed the usage of `np.int16(wav_data).tobytes()` in the `list_to_wav` function to `ensure compatibility with newer versions of` *NumPy*.

## [0.1.0] - 2023-07-12

### Added

- Initial release of the code.
- Implemented the `Signal class` for working with signals.
- Added functions for `memoization` and `signal processing`.
- Included functions for `reading` and `writing` WAV files.
- Added a `color-coding class` colors for terminal output.

### Changed

- Updated the `convolve` method in the Signal class to *use a more efficient matrix-based convolution algorithm*.
- Upgrade dependencies: Python 3.10, art 6.1, asttokens 2.4.1, colorama 0.4.6, executing 2.0.1, future 0.18.3, icecream 2.1.3, lxml 4.9.3, mpmath 1.3.0, numpy 1.26.2, Pygments 2.16.1, six 1.16.0, sympy 1.12, Wave 0.0.2

### Removed

Removed the `DEBUG variable` as it was not actively used in the code.

## Version [0.0.9] - 2023-07-11

### Added

- The `wav_to_list` and `list_to_wav` functions *for converting audio data between WAV files and lists*.

### Changed

- Modified the `generate_filters` method in the `Signal class` to *handle cases where HF does not have the expected structure*.
- Modfied the  `filter_bank_6th_degree` to use `generate_filters` function.

## Version [0.0.8] - 2023-07-11

### Added

- The `factorize_polynomial` method in the `Signal class` for *factorizing signal values as a polynomial*.

### Changed

- Updated the documentation *(doc.html)* for the Signal class methods to provide more clarity.

## Version [0.0.7] - 2023-07-9

### Added
- The `recursive_analysis` method in the `Signal class` for *recursively analyzing signals*.

## Version [0.0.6] - 2023-07-9

### Added

- The `Analysis` method in the `Signal class` for `performing signal analysis through convolution and downsampling`.

## Version [0.0.5] - 2023-07-9

### Added
- The `recursive_synthesis` method in the `Signal class` for *recursively synthesizing signals from a list*.

## Version [0.0.4] - 2023-07-9

### Added
- The `Synthesis` method in the `Signal class` for *recursively synthesizing signals*.

## Version [0.0.3] - 2023-07-6

### Added

- The `filter_bank_6th_degree` method in the `Signal class` for *implementing a filter bank for a 6th-degree polynomial*.

### Changed

- Improved the *structure and organization* of the code.

## Version [0.0.2] - 2023-07-5

### Added 
- The `make_immutable` function within the `memoize decorator` to *recursively convert mutable objects to immutable types*.

## Version [0.0.1] - 2023-07-4

### Added
- Initial development of the code.
- Implemented the `memoize decorator` for *caching function results based on input arguments*.
- Created the `Signal class` with *basic functionality for signal processing (Downsampling, Upsampling, Confolution, etc.)*.