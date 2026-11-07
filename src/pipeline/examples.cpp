#include "pipeline/examples.hpp"

#include <cstddef>
#include <string>
#include <vector>

struct SignalsEmbeddedFile {
    const char* name;
    const char* text;
};
extern const SignalsEmbeddedFile kSignalsExamples[];
extern const std::size_t kSignalsExamplesCount;

namespace signals::pipeline {

std::span<const Example> examples() {
    static const std::vector<Example> all = [] {
        std::vector<Example> out;
        for (std::size_t k = 0; k < kSignalsExamplesCount; ++k) {
            std::string_view name{kSignalsExamples[k].name};
            if (name.ends_with(".sig")) name.remove_suffix(4);
            out.push_back({name, kSignalsExamples[k].text});
        }
        return out;
    }();
    return all;
}

}  // namespace signals::pipeline
