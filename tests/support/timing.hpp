#pragma once

#include <chrono>

namespace signals::test {

#if defined(__SANITIZE_ADDRESS__)
inline constexpr bool kSanitized = true;
#elif defined(__has_feature)
inline constexpr bool kSanitized = __has_feature(address_sanitizer);
#else
inline constexpr bool kSanitized = false;
#endif

// Debug + ASan/UBSan runs these graph walks 8-30x slower than Release; a shared CI runner adds more.
constexpr std::chrono::milliseconds time_budget(std::chrono::milliseconds optimised) {
    return kSanitized ? optimised * 10 : optimised;
}

}  // namespace signals::test
