#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tsunami::core {

enum class Severity {
    trace,
    debug,
    info,
    warning,
    error,
    critical
};

enum class DiagnosticCategory {
    validation,
    configuration,
    input_data,
    preparation,
    execution,
    persistence,
    geospatial,
    numerical,
    coupling,
    cancellation,
    resource,
    unsupported,
    internal
};

struct DiagnosticContextEntry {
    std::string key;
    std::string value;

    [[nodiscard]] auto operator==(const DiagnosticContextEntry&) const -> bool = default;
};

struct Diagnostic {
    std::string code;
    std::string message;
    DiagnosticCategory category{DiagnosticCategory::internal};
    Severity severity{Severity::error};
    std::vector<DiagnosticContextEntry> context;
    std::optional<std::string> cause_code;

    [[nodiscard]] auto valid() const noexcept -> bool { return !code.empty(); }

    auto add_context(std::string key, std::string value) -> Diagnostic&
    {
        auto existing = std::find_if(
            context.begin(),
            context.end(),
            [&](const DiagnosticContextEntry& entry) { return entry.key == key; });
        if (existing != context.end()) {
            existing->value = std::move(value);
        } else {
            context.push_back({std::move(key), std::move(value)});
        }
        std::sort(
            context.begin(),
            context.end(),
            [](const DiagnosticContextEntry& left, const DiagnosticContextEntry& right) {
                return left.key < right.key;
            });
        return *this;
    }

    [[nodiscard]] auto context_value(std::string_view key) const -> std::optional<std::string>
    {
        const auto found = std::find_if(
            context.begin(),
            context.end(),
            [&](const DiagnosticContextEntry& entry) { return entry.key == key; });
        if (found == context.end()) {
            return std::nullopt;
        }
        return found->value;
    }
};

[[nodiscard]] constexpr auto to_string(Severity severity) noexcept -> std::string_view
{
    switch (severity) {
    case Severity::trace:
        return "trace";
    case Severity::debug:
        return "debug";
    case Severity::info:
        return "info";
    case Severity::warning:
        return "warning";
    case Severity::error:
        return "error";
    case Severity::critical:
        return "critical";
    }
    return "error";
}

[[nodiscard]] constexpr auto to_string(DiagnosticCategory category) noexcept -> std::string_view
{
    switch (category) {
    case DiagnosticCategory::validation:
        return "validation";
    case DiagnosticCategory::configuration:
        return "configuration";
    case DiagnosticCategory::input_data:
        return "input_data";
    case DiagnosticCategory::preparation:
        return "preparation";
    case DiagnosticCategory::execution:
        return "execution";
    case DiagnosticCategory::persistence:
        return "persistence";
    case DiagnosticCategory::geospatial:
        return "geospatial";
    case DiagnosticCategory::numerical:
        return "numerical";
    case DiagnosticCategory::coupling:
        return "coupling";
    case DiagnosticCategory::cancellation:
        return "cancellation";
    case DiagnosticCategory::resource:
        return "resource";
    case DiagnosticCategory::unsupported:
        return "unsupported";
    case DiagnosticCategory::internal:
        return "internal";
    }
    return "internal";
}

[[nodiscard]] inline auto context_value(const Diagnostic& diagnostic, std::string_view key) -> std::optional<std::string>
{
    return diagnostic.context_value(key);
}

} // namespace tsunami::core
