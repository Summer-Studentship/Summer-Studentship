#include <tsunami/geo_gdal/GdalGeospatialImporter.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include "GdalAdapterDetail.hpp"

namespace tsunami::geo_gdal
{
    namespace
    {
        struct FeatureCloser
        {
            auto operator()(OGRFeature *feature) const noexcept -> void
            {
                if (feature != nullptr) {
                    OGRFeature::DestroyFeature(feature);
                }
            }
        };

        using FeatureHandle = std::unique_ptr<OGRFeature, FeatureCloser>;

        [[nodiscard]] auto finite(double value) noexcept -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto point_from_ogr(const OGRPoint &point) -> std::optional<tsunami::geo::Point2D>
        {
            if (point.Is3D() || point.IsMeasured() || !finite(point.getX()) || !finite(point.getY())) {
                return std::nullopt;
            }
            return tsunami::geo::Point2D{point.getX(), point.getY()};
        }

        [[nodiscard]] auto line_from_ring(const OGRLineString &line) -> std::optional<std::vector<tsunami::geo::Point2D>>
        {
            if (line.Is3D() || line.IsMeasured() || line.getNumPoints() <= 0) {
                return std::nullopt;
            }
            auto points = std::vector<tsunami::geo::Point2D>{};
            points.reserve(static_cast<std::size_t>(line.getNumPoints()));
            for (int i = 0; i < line.getNumPoints(); ++i) {
                auto point = tsunami::geo::Point2D{line.getX(i), line.getY(i)};
                if (!finite(point.x) || !finite(point.y)) {
                    return std::nullopt;
                }
                points.push_back(point);
            }
            return points;
        }

        [[nodiscard]] auto convert_geometry(const OGRGeometry &geometry) -> std::optional<tsunami::geo::ImportedGeometry>
        {
            if (geometry.IsEmpty() || geometry.Is3D() || geometry.IsMeasured()) {
                return std::nullopt;
            }
            switch (wkbFlatten(geometry.getGeometryType())) {
            case wkbPoint: {
                const auto *point = geometry.toPoint();
                if (point == nullptr) {
                    return std::nullopt;
                }
                auto converted = point_from_ogr(*point);
                if (!converted) {
                    return std::nullopt;
                }
                return tsunami::geo::ImportedGeometry{*converted};
            }
            case wkbLineString: {
                const auto *line = geometry.toLineString();
                if (line == nullptr) {
                    return std::nullopt;
                }
                auto points = line_from_ring(*line);
                if (!points) {
                    return std::nullopt;
                }
                return tsunami::geo::ImportedGeometry{tsunami::geo::LineString2D{std::move(*points)}};
            }
            case wkbPolygon: {
                const auto *polygon = geometry.toPolygon();
                if (polygon == nullptr || polygon->getExteriorRing() == nullptr) {
                    return std::nullopt;
                }
                auto exterior = line_from_ring(*polygon->getExteriorRing());
                if (!exterior) {
                    return std::nullopt;
                }
                auto converted = tsunami::geo::Polygon2D{std::move(*exterior), {}};
                for (int ring = 0; ring < polygon->getNumInteriorRings(); ++ring) {
                    auto interior = line_from_ring(*polygon->getInteriorRing(ring));
                    if (!interior) {
                        return std::nullopt;
                    }
                    converted.interior_rings.push_back(std::move(*interior));
                }
                return tsunami::geo::ImportedGeometry{std::move(converted)};
            }
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] auto geometry_kind_from_layer(OGRwkbGeometryType type) -> std::optional<tsunami::geo::ImportedGeometryKind>
        {
            if (OGR_GT_HasZ(type) || OGR_GT_HasM(type)) {
                return std::nullopt;
            }
            switch (wkbFlatten(type)) {
            case wkbPoint:
                return tsunami::geo::ImportedGeometryKind::point;
            case wkbLineString:
                return tsunami::geo::ImportedGeometryKind::linestring;
            case wkbPolygon:
                return tsunami::geo::ImportedGeometryKind::polygon;
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] auto convert_field_schema(const OGRFieldDefn &field) -> std::optional<tsunami::geo::ImportedFieldSchema>
        {
            auto schema = tsunami::geo::ImportedFieldSchema{};
            schema.name = field.GetNameRef();
            switch (field.GetType()) {
            case OFTInteger:
                schema.type = field.GetSubType() == OFSTBoolean ? tsunami::geo::ImportedFieldType::boolean : tsunami::geo::ImportedFieldType::integer;
                return schema;
            case OFTInteger64:
                schema.type = tsunami::geo::ImportedFieldType::integer64;
                return schema;
            case OFTReal:
                schema.type = tsunami::geo::ImportedFieldType::real;
                return schema;
            case OFTString:
                schema.type = tsunami::geo::ImportedFieldType::string;
                return schema;
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] auto convert_attribute(const OGRFeature &feature, int index, const tsunami::geo::ImportedFieldSchema &schema)
            -> tsunami::geo::ImportedAttribute
        {
            auto attribute = tsunami::geo::ImportedAttribute{schema.name, std::monostate{}};
            if (!feature.IsFieldSetAndNotNull(index)) {
                return attribute;
            }
            switch (schema.type) {
            case tsunami::geo::ImportedFieldType::integer:
                attribute.value = static_cast<std::int64_t>(feature.GetFieldAsInteger(index));
                break;
            case tsunami::geo::ImportedFieldType::integer64:
                attribute.value = static_cast<std::int64_t>(feature.GetFieldAsInteger64(index));
                break;
            case tsunami::geo::ImportedFieldType::real:
                attribute.value = feature.GetFieldAsDouble(index);
                break;
            case tsunami::geo::ImportedFieldType::boolean:
                attribute.value = feature.GetFieldAsInteger(index) != 0;
                break;
            case tsunami::geo::ImportedFieldType::string:
                attribute.value = std::string{feature.GetFieldAsString(index)};
                break;
            }
            return attribute;
        }
    }

    auto import_geopackage_vector_with_gdal(const tsunami::geo::GeospatialImportRequest &request)
        -> tsunami::core::Result<tsunami::geo::VectorImportResult>
    {
        detail::initialise_gdal_once();
        auto resolved = tsunami::geo::resolve_geospatial_import_asset(request, tsunami::geo::GeospatialImportKind::vector, "GPKG");
        if (!resolved) {
            return tsunami::core::failure<tsunami::geo::VectorImportResult>(resolved.error());
        }

        auto dataset = detail::DatasetHandle{static_cast<GDALDataset *>(GDALOpenEx(
            resolved.value().absolute_path.c_str(),
            GDAL_OF_VECTOR | GDAL_OF_READONLY,
            nullptr,
            nullptr,
            nullptr))};
        if (!dataset) {
            return tsunami::core::failure<tsunami::geo::VectorImportResult>(
                detail::gdal_error("geo.import.vector_open_failed", "GDAL could not open vector asset", "geo.import.vector.open", "import_geopackage_vector_with_gdal")
                    .add_context("path", resolved.value().absolute_path.generic_string()));
        }
        const auto *driver = dataset->GetDriver();
        const auto driver_short = driver == nullptr ? std::string{} : std::string{driver->GetDescription()};
        if (driver_short != "GPKG") {
            return tsunami::core::failure<tsunami::geo::VectorImportResult>(
                detail::gdal_error("geo.import.driver_unsupported", "vector driver is not the selected G1 GeoPackage driver", "geo.import.driver.supported", "import_geopackage_vector_with_gdal")
                    .add_context("expected", "GPKG")
                    .add_context("actual", driver_short));
        }

        OGRLayer *layer = nullptr;
        if (request.layer_name) {
            layer = dataset->GetLayerByName(request.layer_name->c_str());
            if (layer == nullptr) {
                return tsunami::core::failure<tsunami::geo::VectorImportResult>(
                    detail::gdal_error("geo.import.vector_layer_missing", "requested GeoPackage layer is missing", "geo.import.vector.layer.exists", "import_geopackage_vector_with_gdal")
                        .add_context("layer_name", *request.layer_name));
            }
        } else {
            if (dataset->GetLayerCount() != 1) {
                return tsunami::core::failure<tsunami::geo::VectorImportResult>(
                    detail::gdal_error("geo.import.vector_layer_ambiguous", "GeoPackage layer is ambiguous without explicit layer name", "geo.import.vector.layer.unambiguous", "import_geopackage_vector_with_gdal")
                        .add_context("actual", std::to_string(dataset->GetLayerCount())));
            }
            layer = dataset->GetLayer(0);
        }
        if (layer == nullptr) {
            return tsunami::core::failure<tsunami::geo::VectorImportResult>(
                detail::gdal_error("geo.import.vector_layer_missing", "GeoPackage layer is missing", "geo.import.vector.layer.exists", "import_geopackage_vector_with_gdal"));
        }
        auto geometry_kind = geometry_kind_from_layer(layer->GetGeomType());
        if (!geometry_kind) {
            return tsunami::core::failure<tsunami::geo::VectorImportResult>(
                detail::gdal_error("geo.import.vector_geometry_unsupported", "GeoPackage layer geometry type is unsupported", "geo.import.vector.geometry.supported", "import_geopackage_vector_with_gdal")
                    .add_context("actual", OGRGeometryTypeToName(layer->GetGeomType())));
        }
        const auto feature_count = layer->GetFeatureCount(TRUE);
        if (feature_count < 0 || static_cast<std::uint64_t>(feature_count) > request.maximum_vector_features) {
            return tsunami::core::failure<tsunami::geo::VectorImportResult>(
                detail::gdal_error("geo.import.vector_feature_limit", "GeoPackage layer exceeds feature limit", "geo.import.vector.feature_limit", "import_geopackage_vector_with_gdal")
                    .add_context("actual", std::to_string(feature_count))
                    .add_context("expected", std::to_string(request.maximum_vector_features)));
        }

        auto schema = std::vector<tsunami::geo::ImportedFieldSchema>{};
        auto *definition = layer->GetLayerDefn();
        for (int field = 0; field < definition->GetFieldCount(); ++field) {
            const auto *field_definition = definition->GetFieldDefn(field);
            auto converted = convert_field_schema(*field_definition);
            if (!converted) {
                return tsunami::core::failure<tsunami::geo::VectorImportResult>(
                    detail::gdal_error("geo.import.vector_field_unsupported", "GeoPackage field type is unsupported", "geo.import.vector.field.supported", "import_geopackage_vector_with_gdal")
                        .add_context("field_name", field_definition->GetNameRef())
                        .add_context("actual", field_definition->GetFieldTypeName(field_definition->GetType())));
            }
            schema.push_back(std::move(*converted));
        }

        auto features = std::vector<tsunami::geo::ImportedVectorFeature>{};
        features.reserve(static_cast<std::size_t>(feature_count));
        auto coordinate_total = std::size_t{0U};
        layer->ResetReading();
        while (auto feature = FeatureHandle{layer->GetNextFeature()}) {
            const auto *geometry = feature->GetGeometryRef();
            if (geometry == nullptr) {
                return tsunami::core::failure<tsunami::geo::VectorImportResult>(
                    detail::gdal_error("geo.import.vector_geometry_unsupported", "GeoPackage feature geometry is missing", "geo.import.vector.geometry.present", "import_geopackage_vector_with_gdal")
                        .add_context("feature_id", std::to_string(feature->GetFID())));
            }
            auto converted_geometry = convert_geometry(*geometry);
            if (!converted_geometry) {
                return tsunami::core::failure<tsunami::geo::VectorImportResult>(
                    detail::gdal_error("geo.import.vector_geometry_unsupported", "GeoPackage feature geometry is unsupported", "geo.import.vector.geometry.supported", "import_geopackage_vector_with_gdal")
                        .add_context("feature_id", std::to_string(feature->GetFID()))
                        .add_context("actual", geometry->getGeometryName()));
            }
            coordinate_total += tsunami::geo::coordinate_count(*converted_geometry);
            if (coordinate_total > request.maximum_geometry_coordinates) {
                return tsunami::core::failure<tsunami::geo::VectorImportResult>(
                    detail::gdal_error("geo.import.vector_coordinate_limit", "GeoPackage layer exceeds coordinate limit", "geo.import.vector.coordinate_limit", "import_geopackage_vector_with_gdal")
                        .add_context("expected", std::to_string(request.maximum_geometry_coordinates))
                        .add_context("actual", std::to_string(coordinate_total)));
            }
            auto attributes = std::vector<tsunami::geo::ImportedAttribute>{};
            attributes.reserve(schema.size());
            for (int field = 0; field < definition->GetFieldCount(); ++field) {
                attributes.push_back(convert_attribute(*feature, field, schema[static_cast<std::size_t>(field)]));
            }
            features.push_back(tsunami::geo::ImportedVectorFeature{
                feature->GetFID(),
                std::move(*converted_geometry),
                std::move(attributes)});
        }

        auto envelope = OGREnvelope{};
        if (layer->GetExtent(&envelope, TRUE) != OGRERR_NONE) {
            return tsunami::core::failure<tsunami::geo::VectorImportResult>(
                detail::gdal_error("geo.import.spatial_reference_invalid", "GeoPackage layer extent is unavailable", "geo.import.vector.extent", "import_geopackage_vector_with_gdal"));
        }
        auto vector_layer = tsunami::geo::make_imported_vector_layer(
            layer->GetName(),
            *geometry_kind,
            tsunami::geo::BoundingBox2D{envelope.MinX, envelope.MinY, envelope.MaxX, envelope.MaxY},
            detail::extract_native_spatial_reference(layer->GetSpatialRef()),
            std::move(schema),
            std::move(features));
        if (!vector_layer) {
            return tsunami::core::failure<tsunami::geo::VectorImportResult>(vector_layer.error());
        }
        auto warnings = std::vector<tsunami::geo::ImportWarning>{};
        if (!resolved.value().asset->byte_size) {
            warnings.push_back({"geo.import.warning.byte_size_missing", "manifest asset byte size is not declared"});
        }
        auto record = tsunami::geo::make_vector_import_record(
            request,
            *resolved.value().dataset,
            *resolved.value().asset,
            driver_short,
            detail::driver_long_name(driver),
            vector_layer.value(),
            coordinate_total,
            std::move(warnings));
        if (!record) {
            return tsunami::core::failure<tsunami::geo::VectorImportResult>(record.error());
        }
        return tsunami::core::success(tsunami::geo::VectorImportResult{std::move(vector_layer.value()), std::move(record.value())});
    }

} // namespace tsunami::geo_gdal
