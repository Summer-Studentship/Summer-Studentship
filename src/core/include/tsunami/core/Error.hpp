#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tsunami/core/Diagnostic.hpp>

namespace tsunami::core {

using ErrorContextEntry = DiagnosticContextEntry;

class Error {
public:
    Error() = default;

    Error(std::string code, std::string message)
        : diagnostic_{std::move(code), std::move(message), DiagnosticCategory::internal, Severity::error, {}, {}}
    {
    }

    Error(std::string code, std::string message, DiagnosticCategory category, Severity severity)
        : diagnostic_{std::move(code), std::move(message), category, severity, {}, {}}
    {
    }

    [[nodiscard]] auto code() const noexcept -> const std::string& { return diagnostic_.code; }
    [[nodiscard]] auto message() const noexcept -> const std::string& { return diagnostic_.message; }
    [[nodiscard]] auto category() const noexcept -> DiagnosticCategory { return diagnostic_.category; }
    [[nodiscard]] auto severity() const noexcept -> Severity { return diagnostic_.severity; }
    [[nodiscard]] auto context() const noexcept -> const std::vector<ErrorContextEntry>& { return diagnostic_.context; }
    [[nodiscard]] auto cause_code() const noexcept -> const std::optional<std::string>& { return diagnostic_.cause_code; }
    [[nodiscard]] auto diagnostic() const noexcept -> const Diagnostic& { return diagnostic_; }
    [[nodiscard]] auto valid() const noexcept -> bool { return diagnostic_.valid(); }
    [[nodiscard]] auto context_value(std::string_view key) const -> std::optional<std::string>
    {
        return diagnostic_.context_value(key);
    }

    auto add_context(std::string key, std::string value) -> Error&
    {
        diagnostic_.add_context(std::move(key), std::move(value));
        return *this;
    }

    auto with_cause_code(std::string code) -> Error&
    {
        diagnostic_.cause_code = std::move(code);
        return *this;
    }

private:
    Diagnostic diagnostic_;
};

} // namespace tsunami::core
