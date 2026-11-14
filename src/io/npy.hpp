#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core/error.hpp"

namespace signals::io {

struct NpArray {
    std::vector<std::size_t> shape;
    std::variant<std::vector<double>, std::vector<std::string>> data;
};

struct NpLimits {
    std::size_t row = std::size_t{1} << 28;
    std::size_t rows = 4096;
};

using NpzEntries = std::vector<std::pair<std::string, NpArray>>;

core::Result<std::string> encode_npy(const NpArray& a);
core::Result<NpArray> decode_npy(std::string_view bytes, const NpLimits& limits = {});
core::Result<void> write_npy(const std::filesystem::path& path, const NpArray& a);
core::Result<NpArray> read_npy(const std::filesystem::path& path, const NpLimits& limits = {});
core::Result<void> write_npz(const std::filesystem::path& path, const NpzEntries& entries);
core::Result<NpzEntries> read_npz(const std::filesystem::path& path, const NpLimits& limits = {});

NpArray np_scalar(double v);
NpArray np_text(std::string s);
NpArray np_vector(std::vector<double> v);
NpArray np_matrix(const std::vector<std::vector<double>>& rows);
const NpArray* np_find(const NpzEntries& entries, std::string_view key);
std::size_t np_count(const NpArray& a);

}  // namespace signals::io
