#include <tsunami/geo/ConstructedCorridor.hpp>

#include <string>
#include <utility>

#include <tsunami/geo/CorridorConstructionRecord.hpp>

namespace tsunami::geo
{
    namespace
    {
        [[nodiscard]] auto reconstruction_error(
            std::string code,
            std::string message,
            const CorridorConstructionRecord &record) -> tsunami::core::Error
        {
            return tsunami::core::Error{
                       std::move(code),
                       std::move(message),
                       tsunami::core::DiagnosticCategory::validation,
                       tsunami::core::Severity::error}
                .add_context("operation", "make_constructed_corridor_from_record")
                .add_context("rule_id", "geo.corridor.reconstruction.record_accepted")
                .add_context("state_changed", "false")
                .add_context("corridor_id", record.identity.corridor_id)
                .add_context("corridor_revision", std::to_string(record.identity.corridor_revision))
                .add_context("trajectory_id", record.identity.trajectory_id);
        }
    } // namespace

    ConstructedCorridor::ConstructedCorridor(
        Polygon2D polygon,
        BoundingBox2D extent,
        CorridorLocalBasis basis,
        CorridorLongitudinalStations stations,
        CorridorSpongeLimits sponge_limits,
        double offshore_width_m,
        double inland_width_m,
        double total_length_m,
        double area_m2,
        double perimeter_m)
        : polygon_{std::move(polygon)},
          extent_{extent},
          basis_{basis},
          stations_{stations},
          sponge_limits_{sponge_limits},
          offshore_width_m_{offshore_width_m},
          inland_width_m_{inland_width_m},
          total_length_m_{total_length_m},
          area_m2_{area_m2},
          perimeter_m_{perimeter_m}
    {
    }

    auto make_constructed_corridor_from_record(
        const CorridorConstructionRecord &record) -> tsunami::core::Result<ConstructedCorridor>
    {
        if (auto valid = validate_corridor_construction_record(record); !valid) {
            return tsunami::core::failure<ConstructedCorridor>(
                reconstruction_error(
                    "geo.corridor.reconstruction.record_invalid",
                    "corridor construction record is not accepted for reconstruction",
                    record)
                    .with_cause_code(valid.error().code()));
        }
        return tsunami::core::success(ConstructedCorridor{
            record.polygon,
            record.extent,
            record.local_basis,
            record.stations,
            record.sponge_limits,
            record.offshore_width_m,
            record.inland_width_m,
            record.total_length_m,
            record.area_m2,
            record.perimeter_m});
    }

} // namespace tsunami::geo
