#pragma once

#include <tsunami/geo/CorridorConstructionRecord.hpp>

namespace tsunami::geo
{
    struct CorridorConstructionRequest
    {
        const tsunami::data::CaseConfiguration *configuration{};
        const tsunami::data::DatasetManifest *manifest{};
        CorridorReferencePointRequest epicentre;
        CorridorReferencePointRequest target;
        CorridorConstructionIdentity identity;
        CorridorConstructionPolicy policy;
    };

    struct CorridorConstructionResult
    {
        ConstructedCorridor corridor;
        CorridorConstructionRecord record;
        CorridorConstructionDiagnostics diagnostics;
    };

    [[nodiscard]] auto make_corridor_local_basis(Point2D epicentre, Point2D target, const CorridorConstructionPolicy &policy)
        -> tsunami::core::Result<CorridorLocalBasis>;
    [[nodiscard]] auto to_corridor_local_coordinates(
        Point2D global,
        Point2D epicentre,
        const CorridorLocalBasis &basis) -> Point2D;
    [[nodiscard]] auto from_corridor_local_coordinates(
        Point2D local,
        Point2D epicentre,
        const CorridorLocalBasis &basis) -> Point2D;
    [[nodiscard]] auto circular_bearing_residual_degrees(double configured, double derived) noexcept -> double;
    [[nodiscard]] auto construct_corridor(const CorridorConstructionRequest &request)
        -> tsunami::core::Result<CorridorConstructionResult>;

} // namespace tsunami::geo
