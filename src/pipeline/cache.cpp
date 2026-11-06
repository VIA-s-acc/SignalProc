#include "pipeline/cache.hpp"

#include <utility>

namespace signals::pipeline {

std::size_t outputs_bytes(const std::vector<Value>& values) {
    std::size_t n = 0;
    for (const auto& v : values) n += value_bytes(v);
    return n;
}

ResultCache::ResultCache(std::size_t budget_bytes) : budget_{budget_bytes} {}

Outputs ResultCache::get(std::uint64_t key) {
    std::lock_guard lock{mutex_};
    const auto it = index_.find(key);
    if (it == index_.end()) return nullptr;
    recent_.splice(recent_.begin(), recent_, it->second);
    return it->second->values;
}

void ResultCache::put(std::uint64_t key, Outputs values) {
    if (!values) return;
    const auto size = outputs_bytes(*values);
    std::lock_guard lock{mutex_};
    if (const auto it = index_.find(key); it != index_.end()) {
        bytes_ -= it->second->bytes;
        recent_.erase(it->second);
        index_.erase(it);
    }
    if (size > budget_) return;
    recent_.push_front({key, std::move(values), size});
    index_[key] = recent_.begin();
    bytes_ += size;
    while (bytes_ > budget_ && !recent_.empty()) {
        bytes_ -= recent_.back().bytes;
        index_.erase(recent_.back().key);
        recent_.pop_back();
    }
}

std::size_t ResultCache::bytes() const {
    std::lock_guard lock{mutex_};
    return bytes_;
}

std::size_t ResultCache::size() const {
    std::lock_guard lock{mutex_};
    return recent_.size();
}

}  // namespace signals::pipeline
