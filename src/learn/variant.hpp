#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace signals::learn {

class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_{seed} {}
    std::uint64_t next();
    long long range(long long lo, long long hi);
    double uniform();
    double normal();

private:
    std::uint64_t state_;
};

std::uint64_t seed_of(std::string_view task_id, int variant, int stream = 0);

struct ParamSpec {
    enum class Kind { integer, ints, real, choice };
    std::string name;
    Kind kind = Kind::integer;
    int count = 1;
    double min = 0, max = 9;
    int decimals = 2;
    std::vector<std::string> choices = {};
};

using ParamValues = std::map<std::string, std::string, std::less<>>;

ParamValues draw_params(std::span<const ParamSpec> specs, std::string_view task_id, int variant);
std::string substitute(std::string_view text, const ParamValues& values);
std::vector<std::string> placeholders(std::string_view text);

}  // namespace signals::learn
