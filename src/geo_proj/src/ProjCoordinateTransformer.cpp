#include <tsunami/geo_proj/ProjCoordinateTransformer.hpp>

#include "ProjAdapterDetail.hpp"

namespace tsunami::geo_proj
{
    auto transformation_runtime_version() -> std::string
    {
        const auto info = proj_info();
        return info.version != nullptr ? std::string{info.version} : std::string{};
    }

    auto transformation_database_version() -> std::string
    {
        auto context = detail::make_context({});
        if (!context) {
            return {};
        }
        const auto *major = proj_context_get_database_metadata(context.value().get(), "DATABASE.LAYOUT.VERSION.MAJOR");
        const auto *minor = proj_context_get_database_metadata(context.value().get(), "DATABASE.LAYOUT.VERSION.MINOR");
        if (major == nullptr) {
            return {};
        }
        auto version = std::string{major};
        if (minor != nullptr) {
            version += ".";
            version += minor;
        }
        return version;
    }

    auto transformation_network_enabled_by_default() -> bool
    {
        auto context = detail::make_context({});
        return context && proj_context_is_network_enabled(context.value().get()) != 0;
    }

} // namespace tsunami::geo_proj
