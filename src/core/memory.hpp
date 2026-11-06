#pragma once

#include <array>
#include <cstddef>

namespace signals::core {

enum class MemoryCategory : std::size_t { signals, coefficients, pipeline_cache, fft_plans, display, count };

inline constexpr std::size_t kMemoryCategories = static_cast<std::size_t>(MemoryCategory::count);

class TrackedBytes {
public:
    TrackedBytes() = default;
    TrackedBytes(MemoryCategory category, std::size_t bytes);
    TrackedBytes(TrackedBytes&& other) noexcept;
    TrackedBytes& operator=(TrackedBytes&& other) noexcept;
    TrackedBytes(const TrackedBytes&) = delete;
    TrackedBytes& operator=(const TrackedBytes&) = delete;
    ~TrackedBytes();

    void reset(std::size_t bytes);
    std::size_t bytes() const { return bytes_; }

private:
    MemoryCategory category_ = MemoryCategory::signals;
    std::size_t bytes_ = 0;
};

std::array<std::size_t, kMemoryCategories> tracked_memory();

}  // namespace signals::core
