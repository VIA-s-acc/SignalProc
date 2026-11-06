#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace signals::theory {

struct Inline {
    enum class Kind { text, strong, emphasis, code, math, ref, placeholder };
    Kind kind = Kind::text;
    std::string text;
    std::string target;
    std::vector<Inline> children;
};

struct Block {
    enum class Kind { heading, paragraph, bullet, numbered, table, display_math, code, rule };
    Kind kind = Kind::paragraph;
    int level = 0;
    int number = 0;
    std::vector<Inline> inlines;
    std::vector<std::vector<std::vector<Inline>>> table;
    std::string text;
    std::string language;
};

std::vector<Block> parse_markdown(std::string_view md);
std::vector<Inline> parse_inlines(std::string_view text);
std::vector<std::string> math_spans(std::string_view md);
std::vector<std::string> unresolved_refs(std::string_view md);

}  // namespace signals::theory
