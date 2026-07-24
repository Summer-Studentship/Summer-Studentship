#pragma once

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

#include <tsunami/core/Error.hpp>

namespace tsunami::core {

template <class T>
class Result {
public:
    static_assert(!std::is_reference_v<T>, "Result<T> does not store references");

    Result(T value) : storage_(std::move(value)) {}
    Result(Error error) : storage_(std::move(error)) {}

    [[nodiscard]] auto has_value() const noexcept -> bool { return std::holds_alternative<T>(storage_); }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

    auto value() & -> T&
    {
        if (!has_value()) {
            throw std::logic_error("accessing value from failed Result");
        }
        return std::get<T>(storage_);
    }

    auto value() const& -> const T&
    {
        if (!has_value()) {
            throw std::logic_error("accessing value from failed Result");
        }
        return std::get<T>(storage_);
    }

    auto value() && -> T
    {
        if (!has_value()) {
            throw std::logic_error("accessing value from failed Result");
        }
        return std::move(std::get<T>(storage_));
    }

    auto error() const& -> const Error&
    {
        if (has_value()) {
            throw std::logic_error("accessing error from successful Result");
        }
        return std::get<Error>(storage_);
    }

private:
    std::variant<T, Error> storage_;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)), success_(false) {}

    [[nodiscard]] auto has_value() const noexcept -> bool { return success_; }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

    auto error() const& -> const Error&
    {
        if (success_) {
            throw std::logic_error("accessing error from successful Result<void>");
        }
        return error_;
    }

private:
    Error error_;
    bool success_{true};
};

template <class T>
auto success(T value) -> Result<T>
{
    return Result<T>(std::move(value));
}

inline auto success() -> Result<void>
{
    return Result<void>();
}

template <class T = void>
auto failure(Error error) -> Result<T>
{
    return Result<T>(std::move(error));
}

} // namespace tsunami::core
