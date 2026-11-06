#include "core/memory.hpp"

#include <atomic>
#include <utility>

namespace signals::core {
namespace {

std::array<std::atomic<std::size_t>, kMemoryCategories>& counters() {
    static std::array<std::atomic<std::size_t>, kMemoryCategories> c{};
    return c;
}

std::atomic<std::size_t>& counter(MemoryCategory c) { return counters()[static_cast<std::size_t>(c)]; }

}  // namespace

TrackedBytes::TrackedBytes(MemoryCategory category, std::size_t bytes) : category_{category}, bytes_{bytes} {
    counter(category_) += bytes_;
}

TrackedBytes::TrackedBytes(TrackedBytes&& other) noexcept
    : category_{other.category_}, bytes_{std::exchange(other.bytes_, 0)} {}

TrackedBytes& TrackedBytes::operator=(TrackedBytes&& other) noexcept {
    if (this != &other) {
        reset(0);
        category_ = other.category_;
        bytes_ = std::exchange(other.bytes_, 0);
    }
    return *this;
}

TrackedBytes::~TrackedBytes() { reset(0); }

void TrackedBytes::reset(std::size_t bytes) {
    counter(category_) -= bytes_;
    bytes_ = bytes;
    counter(category_) += bytes_;
}

std::array<std::size_t, kMemoryCategories> tracked_memory() {
    std::array<std::size_t, kMemoryCategories> out{};
    for (std::size_t i = 0; i < kMemoryCategories; ++i) out[i] = counters()[i].load();
    return out;
}

}  // namespace signals::core
