#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "pipeline/value.hpp"

namespace signals::pipeline {

using Outputs = std::shared_ptr<const std::vector<Value>>;

class ResultCache {
public:
    explicit ResultCache(std::size_t budget_bytes);

    Outputs get(std::uint64_t key);
    void put(std::uint64_t key, Outputs values);
    std::size_t bytes() const;
    std::size_t size() const;
    std::size_t budget() const { return budget_; }

private:
    struct Entry {
        std::uint64_t key;
        Outputs values;
        std::size_t bytes;
    };

    std::size_t budget_;
    std::size_t bytes_ = 0;
    std::list<Entry> recent_;
    std::map<std::uint64_t, std::list<Entry>::iterator> index_;
    mutable std::mutex mutex_;
};

std::size_t outputs_bytes(const std::vector<Value>& values);

}  // namespace signals::pipeline
