#include "ProjAdapterDetail.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace tsunami::geo_proj::detail
{
    namespace
    {
        void discard_proj_log(void *, int, const char *) {}
    }

    auto proj_error(
        std::string code,
        std::string message,
        std::string rule_id,
        std::string operation) -> tsunami::core::Error
    {
        auto error = tsunami::core::Error{
            std::move(code),
            std::move(message),
            tsunami::core::DiagnosticCategory::validation,
            tsunami::core::Severity::error};
        error.add_context("operation", std::move(operation))
            .add_context("rule_id", std::move(rule_id))
            .add_context("state_changed", "false");
        return error;
    }

    auto make_context(const std::filesystem::path &resource_root)
        -> tsunami::core::Result<ContextHandle>
    {
        auto context = ContextHandle{proj_context_create()};
        if (!context) {
            return tsunami::core::failure<ContextHandle>(proj_error("geo.crs.context_creation_failed", "could not create transformation context", "geo.crs.context.create", "create_transformation_context"));
        }
        proj_log_func(context.get(), nullptr, discard_proj_log);
        proj_context_set_enable_network(context.get(), 0);
        if (!resource_root.empty()) {
            const auto generic = resource_root.generic_string();
            const char *paths[] = {generic.c_str()};
            proj_context_set_search_paths(context.get(), 1, paths);
        }
        if (proj_context_is_network_enabled(context.get()) != 0) {
            return tsunami::core::failure<ContextHandle>(proj_error("geo.crs.network_forbidden", "transformation context network access is enabled", "geo.crs.network.disabled", "create_transformation_context"));
        }
        return tsunami::core::success(std::move(context));
    }

} // namespace tsunami::geo_proj::detail
