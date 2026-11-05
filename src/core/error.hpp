#pragma once

#include <expected>
#include <string>

namespace signals::core {

enum class Errc {
    invalid_argument,
    level_out_of_range,
    length_mismatch,
    not_perfect_reconstruction,
    unknown_wavelet,
    no_convergence,
    io_error,
    cancelled,
};

struct Error {
    Errc code;
    std::string message;
};

template <class T>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> make_error(Errc code, std::string message) {
    return std::unexpected<Error>{Error{code, std::move(message)}};
}

}  // namespace signals::core
