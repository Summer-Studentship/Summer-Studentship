#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/core/Result.hpp>
#include <tsunami/data/CaseConfiguration.hpp>
#include <tsunami/data/References.hpp>

namespace tsunami::data
{
    inline constexpr std::string_view dataset_manifest_schema_name{"tsunami.dataset_manifest"};
    inline constexpr tsunami::core::SemanticVersion supported_dataset_manifest_version{1U, 0U, 0U};
    inline constexpr std::string_view supported_dataset_manifest_policy_version{"0.1"};
    inline constexpr std::string_view authoritative_dataset_manifest_path{"manifests/datasets.json"};
    inline constexpr std::string_view dataset_manifest_media_type{"application/json"};

    enum class DatasetManifestCompatibility
    {
        exact,
        patch_equivalent,
        forward_compatible_minor,
        migration_required,
        unsupported_major
    };

    enum class DatasetRole
    {
        bathymetry,
        topography,
        earthquake_displacement,
        prescribed_surface,
        manning,
        coriolis,
        observation,
        auxiliary
    };

    enum class DatasetOriginKind
    {
        source,
        generated
    };

    enum class DatasetRepresentationKind
    {
        raster,
        vector,
        point_series,
        table,
        multidimensional,
        other
    };

    enum class DatasetAssetRole
    {
        primary,
        metadata,
        auxiliary
    };

    enum class DatasetLocationKind
    {
        managed_path,
        external_uri
    };

    enum class DigestAlgorithm
    {
        sha256
    };

    enum class DigestOrigin
    {
        provider_declared,
        project_computed
    };

    enum class SpatialApplicability
    {
        spatial,
        not_applicable
    };

    enum class SpatialResolutionKind
    {
        grid_spacing,
        nominal,
        irregular,
        not_reported,
        not_applicable
    };

    enum class TemporalResolutionKind
    {
        static_dataset,
        interval,
        irregular,
        not_reported,
        not_applicable
    };

    enum class UncertaintyStatus
    {
        reported,
        estimated,
        not_reported,
        not_applicable
    };

    [[nodiscard]] auto to_string(DatasetManifestCompatibility compatibility) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(DatasetRole role) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(DatasetOriginKind kind) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(DatasetRepresentationKind kind) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(DatasetAssetRole role) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(DatasetLocationKind kind) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(DigestAlgorithm algorithm) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(DigestOrigin origin) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(SpatialApplicability applicability) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(SpatialResolutionKind kind) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(TemporalResolutionKind kind) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(UncertaintyStatus status) noexcept -> std::string_view;

    struct DatasetManifestExtension
    {
        std::string name;
        std::string canonical_json;

        [[nodiscard]] auto operator==(const DatasetManifestExtension &) const -> bool = default;
    };

    struct DatasetManifestExtensions
    {
        std::vector<DatasetManifestExtension> values;

        [[nodiscard]] auto operator==(const DatasetManifestExtensions &) const -> bool = default;
    };

    struct DatasetManifestIdentity
    {
        std::string manifest_id;
        std::uint64_t manifest_revision{};
        CaseRevisionRef case_revision;
        std::string created_at_utc;
        std::string created_by;

        [[nodiscard]] auto operator==(const DatasetManifestIdentity &) const -> bool = default;
    };

    struct DatasetProvider
    {
        std::string provider_id;
        std::string name;
        std::optional<std::string> organisation;
        std::optional<std::string> homepage_uri;
        DatasetManifestExtensions extensions;

        [[nodiscard]] auto operator==(const DatasetProvider &) const -> bool = default;
    };

    struct DatasetLicence
    {
        std::string licence_id;
        std::string name;
        std::string expression;
        std::optional<std::string> licence_uri;
        std::optional<std::string> attribution;
        DatasetManifestExtensions extensions;

        [[nodiscard]] auto operator==(const DatasetLicence &) const -> bool = default;
    };

    struct SourceAcquisitionRecord
    {
        std::string source_uri;
        std::string accessed_at_utc;
        std::optional<std::string> source_version;
        std::optional<std::string> publication_date;

        [[nodiscard]] auto operator==(const SourceAcquisitionRecord &) const -> bool = default;
    };

    struct DatasetAssetLocation
    {
        DatasetLocationKind kind{DatasetLocationKind::managed_path};
        std::optional<std::filesystem::path> managed_path;
        std::optional<std::string> external_uri;

        [[nodiscard]] auto operator==(const DatasetAssetLocation &) const -> bool = default;
    };

    struct ContentDigest
    {
        DigestAlgorithm algorithm{DigestAlgorithm::sha256};
        std::string value;
        DigestOrigin origin{DigestOrigin::project_computed};

        [[nodiscard]] auto operator==(const ContentDigest &) const -> bool = default;
    };

    struct DatasetAsset
    {
        std::string asset_id;
        DatasetAssetRole role{DatasetAssetRole::primary};
        DatasetAssetLocation location;
        std::string media_type;
        std::optional<std::uint64_t> byte_size;
        ContentDigest digest;

        [[nodiscard]] auto operator==(const DatasetAsset &) const -> bool = default;
    };

    struct DatasetSpatialReference
    {
        SpatialApplicability applicability{SpatialApplicability::spatial};
        std::optional<std::string> horizontal_crs;
        std::optional<std::string> vertical_datum;
        std::optional<std::string> horizontal_unit;
        std::optional<std::string> vertical_unit;
        std::optional<std::string> axis_order;
        std::optional<std::string> vertical_positive;

        [[nodiscard]] auto operator==(const DatasetSpatialReference &) const -> bool = default;
    };

    struct SpatialResolution
    {
        SpatialResolutionKind kind{SpatialResolutionKind::not_reported};
        std::optional<double> x;
        std::optional<double> y;
        std::optional<std::string> unit;
        std::optional<std::string> description;

        [[nodiscard]] auto operator==(const SpatialResolution &) const -> bool = default;
    };

    struct TemporalResolution
    {
        TemporalResolutionKind kind{TemporalResolutionKind::not_applicable};
        std::optional<double> value;
        std::optional<std::string> unit;
        std::optional<std::string> description;

        [[nodiscard]] auto operator==(const TemporalResolution &) const -> bool = default;
    };

    struct DatasetResolution
    {
        SpatialResolution spatial;
        TemporalResolution temporal;

        [[nodiscard]] auto operator==(const DatasetResolution &) const -> bool = default;
    };

    struct UncertaintyMeasure
    {
        std::string quantity;
        double value{};
        std::string unit;
        std::optional<double> confidence_level;
        std::optional<std::string> method;

        [[nodiscard]] auto operator==(const UncertaintyMeasure &) const -> bool = default;
    };

    struct DatasetUncertainty
    {
        UncertaintyStatus status{UncertaintyStatus::not_reported};
        std::vector<UncertaintyMeasure> measures;
        std::optional<std::string> description;

        [[nodiscard]] auto operator==(const DatasetUncertainty &) const -> bool = default;
    };

    struct DatasetRecord
    {
        std::string dataset_id;
        DatasetOriginKind origin_kind{DatasetOriginKind::source};
        DatasetRepresentationKind representation{DatasetRepresentationKind::other};
        std::vector<DatasetRole> roles;
        std::string title;
        std::optional<std::string> description;
        std::string provider_id;
        std::string licence_id;
        std::optional<SourceAcquisitionRecord> source;
        std::optional<std::string> generated_by_process_id;
        std::vector<DatasetAsset> assets;
        DatasetSpatialReference spatial_reference;
        DatasetResolution resolution;
        DatasetUncertainty uncertainty;
        std::optional<std::string> citation;
        DatasetManifestExtensions extensions;

        [[nodiscard]] auto operator==(const DatasetRecord &) const -> bool = default;
    };

    struct ProcessingSoftware
    {
        std::string name;
        std::string version;
        std::optional<std::string> repository_uri;
        std::optional<std::string> commit_sha;

        [[nodiscard]] auto operator==(const ProcessingSoftware &) const -> bool = default;
    };

    struct ProcessingRecord
    {
        std::string process_id;
        std::string operation;
        std::string executed_at_utc;
        ProcessingSoftware software;
        std::string canonical_parameters_json;
        std::vector<std::string> input_dataset_ids;
        std::vector<std::string> output_dataset_ids;
        DatasetManifestExtensions extensions;

        [[nodiscard]] auto operator==(const ProcessingRecord &) const -> bool = default;
    };

    class DatasetManifest
    {
    public:
        [[nodiscard]] auto schema_identity() const noexcept -> const SchemaIdentity & { return schema_; }
        [[nodiscard]] auto compatibility() const noexcept -> DatasetManifestCompatibility { return compatibility_; }
        [[nodiscard]] auto policy_version() const noexcept -> std::string_view { return policy_version_; }
        [[nodiscard]] auto identity() const noexcept -> const DatasetManifestIdentity & { return identity_; }
        [[nodiscard]] auto providers() const noexcept -> const std::vector<DatasetProvider> & { return providers_; }
        [[nodiscard]] auto licences() const noexcept -> const std::vector<DatasetLicence> & { return licences_; }
        [[nodiscard]] auto datasets() const noexcept -> const std::vector<DatasetRecord> & { return datasets_; }
        [[nodiscard]] auto processes() const noexcept -> const std::vector<ProcessingRecord> & { return processes_; }
        [[nodiscard]] auto extensions() const noexcept -> const DatasetManifestExtensions & { return extensions_; }

        [[nodiscard]] auto find_provider(std::string_view provider_id) const noexcept -> const DatasetProvider *;
        [[nodiscard]] auto find_licence(std::string_view licence_id) const noexcept -> const DatasetLicence *;
        [[nodiscard]] auto find_dataset(std::string_view dataset_id) const noexcept -> const DatasetRecord *;
        [[nodiscard]] auto find_process(std::string_view process_id) const noexcept -> const ProcessingRecord *;

        [[nodiscard]] auto operator==(const DatasetManifest &) const -> bool = default;

    private:
        friend auto make_dataset_manifest(
            SchemaIdentity schema,
            DatasetManifestCompatibility compatibility,
            std::string policy_version,
            DatasetManifestIdentity identity,
            std::vector<DatasetProvider> providers,
            std::vector<DatasetLicence> licences,
            std::vector<DatasetRecord> datasets,
            std::vector<ProcessingRecord> processes,
            DatasetManifestExtensions extensions) -> tsunami::core::Result<DatasetManifest>;

        DatasetManifest(
            SchemaIdentity schema,
            DatasetManifestCompatibility compatibility,
            std::string policy_version,
            DatasetManifestIdentity identity,
            std::vector<DatasetProvider> providers,
            std::vector<DatasetLicence> licences,
            std::vector<DatasetRecord> datasets,
            std::vector<ProcessingRecord> processes,
            DatasetManifestExtensions extensions);

        SchemaIdentity schema_;
        DatasetManifestCompatibility compatibility_{DatasetManifestCompatibility::exact};
        std::string policy_version_;
        DatasetManifestIdentity identity_;
        std::vector<DatasetProvider> providers_;
        std::vector<DatasetLicence> licences_;
        std::vector<DatasetRecord> datasets_;
        std::vector<ProcessingRecord> processes_;
        DatasetManifestExtensions extensions_;
    };

    [[nodiscard]] auto make_dataset_manifest(
        SchemaIdentity schema,
        DatasetManifestCompatibility compatibility,
        std::string policy_version,
        DatasetManifestIdentity identity,
        std::vector<DatasetProvider> providers,
        std::vector<DatasetLicence> licences,
        std::vector<DatasetRecord> datasets,
        std::vector<ProcessingRecord> processes,
        DatasetManifestExtensions extensions) -> tsunami::core::Result<DatasetManifest>;

} // namespace tsunami::data
