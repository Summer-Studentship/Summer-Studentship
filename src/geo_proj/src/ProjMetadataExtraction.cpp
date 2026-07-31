#include "ProjAdapterDetail.hpp"

#include <cmath>
#include <optional>
#include <string>

namespace tsunami::geo_proj::detail
{
    namespace
    {
        [[nodiscard]] auto text_or_null(const char *value) -> std::optional<std::string>
        {
            if (value == nullptr || *value == '\0') {
                return std::nullopt;
            }
            return std::string{value};
        }

        auto fill_axis_metadata(PJ_CONTEXT *context, PJ *crs, tsunami::geo::CoordinateReferenceDescriptor &descriptor) -> void
        {
            auto cs = ObjectHandle{proj_crs_get_coordinate_system(context, crs)};
            if (!cs) {
                return;
            }
            const auto count = proj_cs_get_axis_count(context, cs.get());
            for (int i = 0; i < count; ++i) {
                const char *name = nullptr;
                const char *abbrev = nullptr;
                const char *direction = nullptr;
                const char *unit_name = nullptr;
                const char *unit_auth = nullptr;
                const char *unit_code = nullptr;
                double unit_factor = 0.0;
                if (proj_cs_get_axis_info(context, cs.get(), i, &name, &abbrev, &direction, &unit_factor, &unit_name, &unit_auth, &unit_code) != 0) {
                    descriptor.axis_names.push_back(name != nullptr ? std::string{name} : std::string{});
                    descriptor.axis_directions.push_back(direction != nullptr ? std::string{direction} : std::string{});
                    descriptor.axis_units.push_back(unit_name != nullptr ? std::string{unit_name} : std::string{});
                }
            }
        }
    }

    auto descriptor_definition(const tsunami::geo::CoordinateReferenceDescriptor &descriptor)
        -> std::string
    {
        if (descriptor.authority_name && descriptor.authority_code) {
            return *descriptor.authority_name + ":" + *descriptor.authority_code;
        }
        if (descriptor.canonical_wkt2) {
            return *descriptor.canonical_wkt2;
        }
        if (descriptor.canonical_projjson) {
            return *descriptor.canonical_projjson;
        }
        return descriptor.name;
    }

    auto descriptor_from_object(PJ_CONTEXT *context, PJ *object, const tsunami::geo::CoordinateReferenceDescriptor &fallback)
        -> tsunami::geo::CoordinateReferenceDescriptor
    {
        auto descriptor = fallback;
        descriptor.authority_name = text_or_null(proj_get_id_auth_name(object, 0));
        descriptor.authority_code = text_or_null(proj_get_id_code(object, 0));
        if (const auto *name = proj_get_name(object); name != nullptr && *name != '\0') {
            descriptor.name = name;
        }
        descriptor.canonical_wkt2 = text_or_null(proj_as_wkt(context, object, PJ_WKT2_2019, nullptr));
        descriptor.canonical_projjson = text_or_null(proj_as_projjson(context, object, nullptr));
        descriptor.axis_names.clear();
        descriptor.axis_directions.clear();
        descriptor.axis_units.clear();
        fill_axis_metadata(context, object, descriptor);
        if (descriptor.axis_names.empty()) {
            descriptor.axis_names = fallback.axis_names;
            descriptor.axis_directions = fallback.axis_directions;
            descriptor.axis_units = fallback.axis_units;
        }
        return descriptor;
    }

    auto grids_from_operation(PJ_CONTEXT *context, PJ *operation)
        -> std::vector<tsunami::geo::CoordinateOperationGrid>
    {
        auto grids = std::vector<tsunami::geo::CoordinateOperationGrid>{};
        const auto count = proj_coordoperation_get_grid_used_count(context, operation);
        for (int i = 0; i < count; ++i) {
            const char *short_name = nullptr;
            const char *full_name = nullptr;
            const char *package_name = nullptr;
            const char *url = nullptr;
            int direct_download = 0;
            int open_license = 0;
            int available = 0;
            if (proj_coordoperation_get_grid_used(context, operation, i, &short_name, &full_name, &package_name, &url, &direct_download, &open_license, &available) != 0) {
                grids.push_back(tsunami::geo::CoordinateOperationGrid{
                    short_name != nullptr ? std::string{short_name} : std::string{},
                    text_or_null(full_name),
                    text_or_null(package_name),
                    text_or_null(url),
                    available != 0,
                    open_license != 0,
                    std::nullopt,
                    available != 0 ? tsunami::geo::GeodeticResourceVerificationStatus::declared_not_verified : tsunami::geo::GeodeticResourceVerificationStatus::unavailable});
            }
        }
        return grids;
    }

} // namespace tsunami::geo_proj::detail
