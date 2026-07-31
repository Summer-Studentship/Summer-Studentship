#include "ProjAdapterDetail.hpp"

namespace tsunami::geo_proj::detail
{
    auto make_base_record(
        const tsunami::geo::CoordinateTransformationRequest &request,
        const tsunami::geo::CoordinateOperationRecord &operation,
        tsunami::geo::BoundingBox2D source_extent,
        tsunami::geo::BoundingBox2D target_extent,
        tsunami::geo::CoordinateTransformationDiagnostics diagnostics)
        -> tsunami::core::Result<tsunami::geo::CoordinateTransformationRecord>
    {
        auto source = tsunami::geo::source_horizontal_reference_from_import_record(*request.source_import_record);
        if (!source) {
            return tsunami::core::failure<tsunami::geo::CoordinateTransformationRecord>(source.error());
        }
        auto record = tsunami::geo::CoordinateTransformationRecord{};
        record.schema = tsunami::data::SchemaIdentity{
            std::string{tsunami::geo::coordinate_transformation_record_schema_name},
            tsunami::geo::supported_coordinate_transformation_record_version};
        record.policy_version = tsunami::geo::supported_coordinate_transformation_record_policy_version;
        record.identity = request.identity;
        record.source_horizontal = source.value();
        if (request.source_import_record->datum_evidence.vertical) {
            auto vertical = tsunami::geo::CoordinateReferenceDescriptor{};
            vertical.name = request.source_import_record->datum_evidence.vertical->datum_name;
            vertical.datum_name = request.source_import_record->datum_evidence.vertical->datum_name;
            vertical.datum_realisation = request.source_import_record->datum_evidence.vertical->datum_realisation;
            vertical.authority_name = request.source_import_record->datum_evidence.vertical->authority_name;
            vertical.authority_code = request.source_import_record->datum_evidence.vertical->authority_code;
            vertical.axis_names = {"height"};
            vertical.axis_directions = {request.source_import_record->datum_evidence.vertical->positive_direction.value_or("up")};
            vertical.axis_units = {request.source_import_record->datum_evidence.vertical->unit};
            record.source_vertical = vertical;
        }
        record.target = request.target;
        record.area_of_interest = request.selection_policy.area_of_interest;
        record.horizontal_operation = operation;
        record.vertical_operation = request.vertical;
        record.storage_axes = request.target.storage_axes;
        record.grids = operation.grids;
        record.source_extent = source_extent;
        record.target_extent = target_extent;
        record.diagnostics = std::move(diagnostics);
        record.warnings = {};
        if (auto valid = tsunami::geo::validate_coordinate_transformation_record(record); !valid) {
            return tsunami::core::failure<tsunami::geo::CoordinateTransformationRecord>(valid.error());
        }
        return tsunami::core::success(std::move(record));
    }

} // namespace tsunami::geo_proj::detail
