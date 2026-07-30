#include <tsunami/geo_gdal/GdalGeospatialImporter.hpp>

#include <mutex>

#include <gdal_priv.h>
#include <gdal_version.h>

#include "GdalAdapterDetail.hpp"

namespace tsunami::geo_gdal
{
    namespace detail
    {
        void initialise_gdal_once()
        {
            static auto once = std::once_flag{};
            std::call_once(once, [] {
                GDALAllRegister();
            });
        }

        auto gdal_error(std::string code, std::string message, std::string rule_id, std::string operation)
            -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::input_data,
                tsunami::core::Severity::error};
            error.add_context("operation", std::move(operation))
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }
    }

    auto gdal_runtime_version() -> std::string
    {
        detail::initialise_gdal_once();
        return GDALVersionInfo("RELEASE_NAME");
    }

    auto gdal_driver_available(std::string_view short_name) -> bool
    {
        detail::initialise_gdal_once();
        return GetGDALDriverManager()->GetDriverByName(std::string{short_name}.c_str()) != nullptr;
    }

} // namespace tsunami::geo_gdal
