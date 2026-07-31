#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <tsunami/core/Result.hpp>
#include <tsunami/fvm/FiniteVolumeMesh.hpp>
#include <tsunami/geo/ConditionedTerrainRaster.hpp>
#include <tsunami/geo/ConstructedCorridor.hpp>
#include <tsunami/geo/CorridorConstructionRecord.hpp>
#include <tsunami/geo/TerrainConditioningRecord.hpp>

namespace tsunami::r2d
{
    struct RegionalMeshImportPhysicalGroups
    {
        std::map<std::string, std::int64_t> physical_name_tags;
    };

    struct RegionalGeometryPreflightRequest
    {
        const tsunami::geo::ConstructedCorridor *corridor{};
        const tsunami::geo::CorridorConstructionRecord *corridor_record{};
        const tsunami::geo::ConditionedTerrainRaster *terrain{};
        const tsunami::geo::TerrainConditioningRecord *terrain_record{};
        const tsunami::fvm::FiniteVolumeMesh *mesh{};
        RegionalMeshImportPhysicalGroups import_physical_groups;
    };

    struct RegionalGeometryPreflightPatchReport
    {
        std::string name;
        std::size_t face_count{};
    };

    struct RegionalGeometryPreflightReport
    {
        std::string validation_status;
        std::string corridor_id;
        std::string terrain_id;
        std::string mesh_id;
        std::size_t vertex_count{};
        std::size_t cell_count{};
        std::size_t face_count{};
        std::size_t internal_face_count{};
        std::size_t boundary_face_count{};
        std::vector<RegionalGeometryPreflightPatchReport> patches;
        tsunami::geo::BoundingBox2D mesh_bounds;
        tsunami::geo::BoundingBox2D terrain_support_bounds;
        double minimum_cell_measure{};
        double minimum_face_length{};
    };

    [[nodiscard]] auto validate_regional2d_geometry_preflight(
        const RegionalGeometryPreflightRequest &request)
        -> tsunami::core::Result<RegionalGeometryPreflightReport>;

} // namespace tsunami::r2d
