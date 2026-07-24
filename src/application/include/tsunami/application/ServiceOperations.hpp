#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <tsunami/core/Identity.hpp>
#include <tsunami/data/References.hpp>

namespace tsunami::application {

enum class ValidationOutcome {
    accepted,
    invalid
};

enum class WorkflowKind {
    regional_2d,
    local_3d,
    coupling_replay
};

enum class RunStateRecommendation {
    created,
    queued,
    cancelling,
    cancelled,
    unknown
};

struct ValidationRequest {
    std::filesystem::path case_location;
    std::optional<tsunami::core::CaseId> case_id;
    std::uint64_t case_revision{};
    std::string scope{"case"};
};

struct ValidationResponse {
    ValidationOutcome outcome{ValidationOutcome::invalid};
    std::vector<tsunami::data::ArtifactRef> validation_artifacts;
    bool state_changed{};
};

struct PreparationRequest {
    tsunami::data::CaseRevisionRef case_revision;
    std::string scope{"execution"};
};

struct PreparationResponse {
    tsunami::core::PreparationId preparation_id;
    tsunami::data::CaseRevisionRef case_revision;
    std::vector<tsunami::data::ArtifactRef> artifacts;
    std::string lifecycle_state_recommendation{"preparing"};
    bool state_changed{};
};

struct RestartReference {
    tsunami::core::RunId parent_run_id;
    std::optional<tsunami::core::CheckpointId> checkpoint_id;
};

struct LaunchRequest {
    tsunami::data::CaseRevisionRef case_revision;
    tsunami::core::PreparationId preparation_id;
    WorkflowKind workflow{WorkflowKind::regional_2d};
    std::optional<RestartReference> restart;
};

struct LaunchResponse {
    tsunami::core::RunId run_id;
    RunStateRecommendation initial_state{RunStateRecommendation::created};
    std::optional<RestartReference> restart_parent;
    std::optional<tsunami::data::ArtifactRef> run_record_artifact;
    bool state_changed{};
};

struct CancellationRequest {
    tsunami::core::RunId run_id;
    std::string reason;
};

struct CancellationResponse {
    tsunami::core::RunId run_id;
    bool accepted{};
    RunStateRecommendation observed_or_recommended_state{RunStateRecommendation::unknown};
    bool completed_synchronously{};
    bool state_changed{};
};

struct ResultDiscoveryRequest {
    tsunami::core::CaseId case_id;
    std::optional<std::uint64_t> case_revision;
    std::optional<tsunami::core::RunId> run_id;
    std::optional<std::string> result_category;
};

struct ResultDiscoveryResponse {
    std::vector<tsunami::data::RunRef> runs;
    std::vector<tsunami::data::ArtifactRef> artifacts;
    std::string metadata;
};

} // namespace tsunami::application
