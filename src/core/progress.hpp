#pragma once

namespace signals::core {

class ProgressSink {
public:
    virtual ~ProgressSink() = default;
    virtual void report(double fraction) = 0;
    virtual bool cancelled() const = 0;
};

}  // namespace signals::core
