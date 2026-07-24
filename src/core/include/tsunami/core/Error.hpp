#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tsunami::core {

struct ErrorContextEntry {
    std::string key;
    std::string value;
};

class Error {
public:
    Error() = default;

    Error(std::string code, std::string message)
        : code_(std::move(code)), message_(std::move(message))
    {
    }

    [[nodiscard]] auto code() const noexcept -> const std::string& { return code_; }
    [[nodiscard]] auto message() const noexcept -> const std::string& { return message_; }
    [[nodiscard]] auto context() const noexcept -> const std::vector<ErrorContextEntry>& { return context_; }
    [[nodiscard]] auto cause_code() const noexcept -> const std::optional<std::string>& { return cause_code_; }
    [[nodiscard]] auto valid() const noexcept -> bool { return !code_.empty(); }

    auto add_context(std::string key, std::string value) -> Error&
    {
        context_.push_back({std::move(key), std::move(value)});
        return *this;
    }

    auto with_cause_code(std::string code) -> Error&
    {
        cause_code_ = std::move(code);
        return *this;
    }

private:
    std::string code_;
    std::string message_;
    std::vector<ErrorContextEntry> context_;
    std::optional<std::string> cause_code_;
};

} // namespace tsunami::core
