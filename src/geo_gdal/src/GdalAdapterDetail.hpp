#pragma once

#include <memory>
#include <string>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <tsunami/core/Error.hpp>
#include <tsunami/geo/SpatialReferenceEvidence.hpp>

namespace tsunami::geo_gdal::detail
{
    struct DatasetCloser
    {
        auto operator()(GDALDataset *dataset) const noexcept -> void
        {
            if (dataset != nullptr) {
                GDALClose(dataset);
            }
        }
    };

    using DatasetHandle = std::unique_ptr<GDALDataset, DatasetCloser>;

    void initialise_gdal_once();

    [[nodiscard]] auto gdal_error(
        std::string code,
        std::string message,
        std::string rule_id,
        std::string operation) -> tsunami::core::Error;

    [[nodiscard]] auto extract_native_spatial_reference(const OGRSpatialReference *reference)
        -> tsunami::geo::NativeSpatialReference;

    [[nodiscard]] auto driver_long_name(const GDALDriver *driver) -> std::string;

} // namespace tsunami::geo_gdal::detail
