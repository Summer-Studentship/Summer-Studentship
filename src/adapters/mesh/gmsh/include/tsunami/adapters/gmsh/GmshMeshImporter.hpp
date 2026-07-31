#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include <tsunami/core/Result.hpp>
#include <tsunami/fvm/FiniteVolumeMesh.hpp>

namespace tsunami::adapters::gmsh
{

    struct GmshMeshImportMetadata
    {
        std::filesystem::path source_path;
        std::string msh_version;
        std::uint64_t imported_node_count{};
        std::uint64_t triangle_count{};
        std::uint64_t boundary_line_count{};
        std::uint64_t clockwise_triangle_count{};
        std::map<std::string, std::int64_t> physical_name_tags;
    };

    struct GmshMeshImportResult
    {
        tsunami::fvm::FiniteVolumeMesh mesh;
        GmshMeshImportMetadata metadata;
    };

    [[nodiscard]] auto import_gmsh_msh41_ascii_mesh(const std::filesystem::path &path)
        -> tsunami::core::Result<GmshMeshImportResult>;

} // namespace tsunami::adapters::gmsh
