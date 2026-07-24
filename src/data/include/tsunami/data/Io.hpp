#pragma once

#include <memory>
#include <string>

#include <tsunami/core/Result.hpp>
#include <tsunami/data/References.hpp>

namespace tsunami::data {

struct ArtifactMetadata {
    ArtifactRef artifact;
    SchemaIdentity schema;
    bool complete{};
};

class IArtifactTransaction {
public:
    virtual ~IArtifactTransaction() = default;

    virtual auto commit() -> tsunami::core::Result<void> = 0;
    virtual auto abort() noexcept -> void = 0;
};

class IArtifactPort {
public:
    virtual ~IArtifactPort() = default;

    virtual auto inspect(const ArtifactRef& artifact) const -> tsunami::core::Result<ArtifactMetadata> = 0;
    virtual auto read_metadata(const ReadRequest& request) const -> tsunami::core::Result<ArtifactMetadata> = 0;
    virtual auto write_metadata(const WriteRequest& request) -> tsunami::core::Result<void> = 0;
    virtual auto begin_transaction(const WriteRequest& request)
        -> tsunami::core::Result<std::unique_ptr<IArtifactTransaction>> = 0;
};

} // namespace tsunami::data
