#include <tsunami/data/CaseConfigurationValidation.hpp>

#include <array>
#include <charconv>
#include <limits>
#include <string>

namespace tsunami::data
{
    namespace
    {
        [[nodiscard]] auto version_error(std::string message) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                "data.case_config.schema_version_invalid",
                std::move(message),
                tsunami::core::DiagnosticCategory::configuration,
                tsunami::core::Severity::error};
            error.add_context("operation", "parse_semantic_version")
                .add_context("rule_id", "case.schema_version.required")
                .add_context("json_pointer", "/schema_version")
                .add_context("state_changed", "false");
            return error;
        }
    } // namespace

    auto parse_semantic_version(std::string_view text) -> tsunami::core::Result<tsunami::core::SemanticVersion>
    {
        auto values = std::array<std::uint32_t, 3>{};
        auto begin = text.data();
        const auto *const end = text.data() + text.size();
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (begin == end || *begin < '0' || *begin > '9') {
                return tsunami::core::failure<tsunami::core::SemanticVersion>(version_error("semantic version component is missing"));
            }
            std::uint64_t parsed{};
            const auto [ptr, ec] = std::from_chars(begin, end, parsed);
            if (ec != std::errc{} || parsed > std::numeric_limits<std::uint32_t>::max()) {
                return tsunami::core::failure<tsunami::core::SemanticVersion>(version_error("semantic version component is invalid or overflows"));
            }
            values[index] = static_cast<std::uint32_t>(parsed);
            begin = ptr;
            if (index + 1U < values.size()) {
                if (begin == end || *begin != '.') {
                    return tsunami::core::failure<tsunami::core::SemanticVersion>(version_error("semantic version requires three dot-separated components"));
                }
                ++begin;
            }
        }
        if (begin != end) {
            return tsunami::core::failure<tsunami::core::SemanticVersion>(version_error("semantic version contains trailing characters"));
        }
        return tsunami::core::success(tsunami::core::SemanticVersion{values[0], values[1], values[2]});
    }

    auto classify_case_schema_version(const tsunami::core::SemanticVersion &version) noexcept -> CaseSchemaCompatibility
    {
        if (version.major == supported_case_configuration_version.major &&
            version.minor == supported_case_configuration_version.minor &&
            version.patch == supported_case_configuration_version.patch) {
            return CaseSchemaCompatibility::exact;
        }
        if (version.major == 0U) {
            return CaseSchemaCompatibility::migration_required;
        }
        if (version.major > supported_case_configuration_version.major) {
            return CaseSchemaCompatibility::unsupported_major;
        }
        if (version.minor == supported_case_configuration_version.minor) {
            return CaseSchemaCompatibility::patch_equivalent;
        }
        return CaseSchemaCompatibility::forward_compatible_minor;
    }

} // namespace tsunami::data
