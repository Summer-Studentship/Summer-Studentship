#pragma once

#include <string>

#include <tsunami/geo/GeospatialImport.hpp>

namespace tsunami::geo_gdal
{
    [[nodiscard]] auto gdal_runtime_version() -> std::string;
    [[nodiscard]] auto gdal_driver_available(std::string_view short_name) -> bool;

    [[nodiscard]] auto import_geotiff_with_gdal(const tsunami::geo::GeospatialImportRequest &request)
        -> tsunami::core::Result<tsunami::geo::RasterImportResult>;

    [[nodiscard]] auto import_geopackage_vector_with_gdal(const tsunami::geo::GeospatialImportRequest &request)
        -> tsunami::core::Result<tsunami::geo::VectorImportResult>;

} // namespace tsunami::geo_gdal
