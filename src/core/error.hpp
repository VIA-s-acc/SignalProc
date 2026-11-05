#pragma once

#include <expected>
#include <string>
#include <string_view>

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

constexpr std::string_view errc_name(Errc code) {
    switch (code) {
    case Errc::invalid_argument: return "invalid_argument";
    case Errc::level_out_of_range: return "level_out_of_range";
    case Errc::length_mismatch: return "length_mismatch";
    case Errc::not_perfect_reconstruction: return "not_perfect_reconstruction";
    case Errc::unknown_wavelet: return "unknown_wavelet";
    case Errc::no_convergence: return "no_convergence";
    case Errc::io_error: return "io_error";
    case Errc::cancelled: return "cancelled";
    }
    return "invalid_argument";
}

inline std::unexpected<Error> make_error(Errc code, std::string message) {
    return std::unexpected<Error>{Error{code, std::move(message)}};
}

}  // namespace signals::core
