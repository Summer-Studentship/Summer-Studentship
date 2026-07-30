#include "GdalAdapterDetail.hpp"

#include <string>

#include <cpl_conv.h>

namespace tsunami::geo_gdal::detail
{
    auto driver_long_name(const GDALDriver *driver) -> std::string
    {
        if (driver == nullptr) {
            return {};
        }
        auto *mutable_driver = const_cast<GDALDriver *>(driver);
        if (const auto *name = mutable_driver->GetMetadataItem(GDAL_DMD_LONGNAME); name != nullptr) {
            return name;
        }
        return mutable_driver->GetDescription();
    }

    auto extract_native_spatial_reference(const OGRSpatialReference *reference)
        -> tsunami::geo::NativeSpatialReference
    {
        auto native = tsunami::geo::NativeSpatialReference{};
        if (reference == nullptr) {
            return native;
        }
        if (const auto *name = reference->GetAuthorityName(nullptr); name != nullptr) {
            native.authority_name = name;
        }
        if (const auto *code = reference->GetAuthorityCode(nullptr); code != nullptr) {
            native.authority_code = code;
        }
        if (const auto *name = reference->GetName(); name != nullptr) {
            native.crs_name = name;
        }
        if (const auto *datum = reference->GetAttrValue("DATUM"); datum != nullptr) {
            native.datum_name = datum;
        }
        const char *options[] = {"FORMAT=WKT2_2019", nullptr};
        char *wkt = nullptr;
        if (reference->exportToWkt(&wkt, options) == OGRERR_NONE && wkt != nullptr) {
            native.canonical_wkt2 = wkt;
        }
        CPLFree(wkt);

        const auto axis_count = reference->GetAxesCount();
        const char *unit_name = nullptr;
        if (reference->IsGeographic()) {
            static_cast<void>(reference->GetAngularUnits(&unit_name));
        } else {
            static_cast<void>(reference->GetLinearUnits(&unit_name));
        }
        for (int axis = 0; axis < axis_count; ++axis) {
            OGRAxisOrientation orientation = OAO_Other;
            if (const auto *axis_name = reference->GetAxis(nullptr, axis, &orientation); axis_name != nullptr) {
                native.axis_names.emplace_back(axis_name);
            }
            native.axis_directions.emplace_back(OSRAxisEnumToName(orientation));
            if (unit_name != nullptr) {
                native.axis_units.emplace_back(unit_name);
            }
        }
        if (reference->IsDynamic()) {
            if (const auto epoch = reference->GetCoordinateEpoch(); epoch > 0.0) {
                native.coordinate_epoch = std::to_string(epoch);
            }
        }
        return native;
    }
} // namespace tsunami::geo_gdal::detail
