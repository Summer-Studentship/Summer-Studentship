#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <tsunami/core/Identity.hpp>

namespace tsunami::data {

struct CaseRevisionRef {
    tsunami::core::CaseId case_id;
    std::uint64_t revision{};

    [[nodiscard]] auto operator==(const CaseRevisionRef&) const -> bool = default;
};

struct PreparationRef {
    tsunami::core::PreparationId preparation_id;
    CaseRevisionRef case_revision;
};

struct RunRef {
    tsunami::core::RunId run_id;
    CaseRevisionRef case_revision;
};

struct ArtifactRef {
    tsunami::core::ArtifactId artifact_id;
    std::filesystem::path managed_path;
    std::string media_type;
};

struct SchemaIdentity {
    std::string schema_name;
    tsunami::core::SemanticVersion version;

    [[nodiscard]] auto operator==(const SchemaIdentity&) const -> bool = default;
};

struct ProvenanceRef {
    std::string category;
    std::string digest;
};

enum class WriteDisposition {
    create,
    version,
    reject_overwrite
};

struct ReadRequest {
    ArtifactRef artifact;
    std::optional<SchemaIdentity> expected_schema;
};

struct WriteRequest {
    ArtifactRef artifact;
    SchemaIdentity schema;
    WriteDisposition disposition{WriteDisposition::reject_overwrite};
    std::vector<ProvenanceRef> provenance;
};

} // namespace tsunami::data
