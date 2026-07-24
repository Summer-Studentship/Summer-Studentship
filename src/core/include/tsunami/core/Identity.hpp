#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace tsunami::core {

class Identifier {
public:
    Identifier() = default;

    static auto from_string(std::string value) -> std::optional<Identifier>
    {
        if (value.empty()) {
            return std::nullopt;
        }
        return Identifier(std::move(value));
    }

    [[nodiscard]] auto valid() const noexcept -> bool { return !value_.empty(); }
    [[nodiscard]] auto str() const noexcept -> const std::string& { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return valid(); }

    friend auto operator<=>(const Identifier&, const Identifier&) = default;

protected:
    explicit Identifier(std::string value) : value_(std::move(value)) {}

private:
    std::string value_;
};

template <class Tag>
class StrongId {
public:
    StrongId() = default;

    static auto from_string(std::string value) -> std::optional<StrongId>
    {
        auto id = Identifier::from_string(std::move(value));
        if (!id) {
            return std::nullopt;
        }
        return StrongId(std::move(*id));
    }

    [[nodiscard]] auto valid() const noexcept -> bool { return id_.valid(); }
    [[nodiscard]] auto str() const noexcept -> const std::string& { return id_.str(); }
    [[nodiscard]] explicit operator bool() const noexcept { return valid(); }

    friend auto operator<=>(const StrongId&, const StrongId&) = default;

private:
    explicit StrongId(Identifier id) : id_(std::move(id)) {}

    Identifier id_;
};

struct CaseIdTag {};
struct PreparationIdTag {};
struct RunIdTag {};
struct CheckpointIdTag {};
struct ArtifactIdTag {};

using CaseId = StrongId<CaseIdTag>;
using PreparationId = StrongId<PreparationIdTag>;
using RunId = StrongId<RunIdTag>;
using CheckpointId = StrongId<CheckpointIdTag>;
using ArtifactId = StrongId<ArtifactIdTag>;

struct SemanticVersion {
    std::uint32_t major{};
    std::uint32_t minor{};
    std::uint32_t patch{};

    [[nodiscard]] auto valid() const noexcept -> bool
    {
        return major > 0U || minor > 0U || patch > 0U;
    }

    [[nodiscard]] auto text() const -> std::string
    {
        return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    }

    friend auto operator<=>(const SemanticVersion&, const SemanticVersion&) = default;
};

template <class Tag>
auto operator<<(std::ostream& os, const StrongId<Tag>& id) -> std::ostream&
{
    return os << id.str();
}

} // namespace tsunami::core
