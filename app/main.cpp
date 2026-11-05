#include <iostream>
#include <string_view>

#include "core/version.hpp"

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view{argv[1]} == "--version") {
        std::cout << "signals " << signals::core::version() << '\n';
        return 0;
    }
    return 0;
}
