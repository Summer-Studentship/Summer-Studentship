#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <tsunami/core/Diagnostic.hpp>
#include <tsunami/core/Error.hpp>

namespace tsunami::core {

enum class OperationState {
    idle,
    accepted,
    running,
    succeeded,
    failed,
    cancelled
};

struct Progress {
    double fraction{};

    [[nodiscard]] auto valid() const noexcept -> bool
    {
        return std::isfinite(fraction) && fraction >= 0.0 && fraction <= 1.0;
    }
};

struct OperationStatus {
    std::string operation;
    OperationState state{OperationState::idle};
    Diagnostic diagnostic;
    std::optional<Progress> progress;
};

[[nodiscard]] inline auto failed_status(std::string operation, const Error& error) -> OperationStatus
{
    return {std::move(operation), OperationState::failed, error.diagnostic(), {}};
}

[[nodiscard]] constexpr auto to_string(OperationState state) noexcept -> std::string_view
{
    switch (state) {
    case OperationState::idle:
        return "idle";
    case OperationState::accepted:
        return "accepted";
    case OperationState::running:
        return "running";
    case OperationState::succeeded:
        return "succeeded";
    case OperationState::failed:
        return "failed";
    case OperationState::cancelled:
        return "cancelled";
    }
    return "idle";
}

} // namespace tsunami::core
