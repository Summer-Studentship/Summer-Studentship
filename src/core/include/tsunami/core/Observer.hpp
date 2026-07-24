#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tsunami::core {

struct ObservationContextEntry {
    std::string key;
    std::string value;
};

struct Progress {
    double fraction{};

    [[nodiscard]] auto valid() const noexcept -> bool
    {
        return fraction >= 0.0 && fraction <= 1.0;
    }
};

struct Observation {
    std::string topic_code;
    std::string message;
    std::optional<Progress> progress;
    std::vector<ObservationContextEntry> context;
};

class IObserver {
public:
    virtual ~IObserver() = default;

    virtual void observe(const Observation& observation) = 0;
};

class NullObserver final : public IObserver {
public:
    void observe(const Observation&) override {}
};

} // namespace tsunami::core
