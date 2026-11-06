#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"

namespace signals::theory {

struct CardSection {
    std::string heading;
    std::string body;
};

struct Card {
    std::string id, title;
    std::vector<std::string> theory;
    std::string source, function, test;
    std::vector<std::string> views;
    std::vector<CardSection> sections;

    const CardSection* section(std::string_view heading) const;
};

core::Result<Card> parse_card(std::string_view text);
const std::vector<Card>& cards();
const Card* find_card(std::string_view id);

struct CardIssue {
    std::string card, message;
};

std::vector<std::string> required_card_sections();
std::vector<std::string_view> known_views();
std::vector<CardIssue> lint_card(const Card& card, const std::filesystem::path& repo_root);
std::vector<CardIssue> lint_cards(const std::filesystem::path& repo_root);

}  // namespace signals::theory
