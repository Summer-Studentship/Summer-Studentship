#pragma once

#include <memory>

namespace tsunami::core {

class ICancellationToken {
public:
    virtual ~ICancellationToken() = default;

    [[nodiscard]] virtual auto stop_requested() const noexcept -> bool = 0;
};

class NeverCancelToken final : public ICancellationToken {
public:
    [[nodiscard]] auto stop_requested() const noexcept -> bool override { return false; }
};

using CancellationTokenRef = const ICancellationToken&;
using SharedCancellationToken = std::shared_ptr<const ICancellationToken>;

} // namespace tsunami::core
