#pragma once

#include <tsunami/core/Status.hpp>

namespace tsunami::core {

using Observation = OperationStatus;

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
