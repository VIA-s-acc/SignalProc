#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace signals::theory {

std::optional<std::string_view> embedded_source(std::string_view path);
std::optional<std::size_t> source_line(std::string_view text, std::string_view function);

struct Piece {
    enum class Kind { plain, keyword, type, function, string, number, comment, preproc };
    Kind kind = Kind::plain;
    std::string text;
};

std::vector<Piece> highlight_cpp(std::string_view line, bool& in_block_comment);

struct Definition {
    std::string path;
    std::size_t line = 0;
};

std::optional<Definition> find_definition(std::string_view name);
std::optional<std::string> resolve_include(std::string_view from, std::string_view include);

}  // namespace signals::theory
