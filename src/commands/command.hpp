#pragma once

#include <memory>
#include <ostream>
#include <vector>

#include "core/error.hpp"

namespace CLI {
class App;
}

namespace signals::cli {

class Command {
public:
    virtual ~Command() = default;
    virtual bool parsed() const = 0;
    virtual int run(std::ostream& out, std::ostream& err) = 0;
};

using Commands = std::vector<std::unique_ptr<Command>>;

void add_audio_commands(CLI::App& app, Commands& commands);
void add_analysis_commands(CLI::App& app, CLI::App& filter, Commands& commands);

int report_error(std::ostream& err, const core::Error& error);

}  // namespace signals::cli
