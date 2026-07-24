#pragma once

#include <string>

#include <tsunami/core/Identity.hpp>

namespace tsunami::application {

struct OperationCapability {
    bool available{};
    std::string reason;
};

struct ServiceCapabilities {
    std::string implementation_id;
    tsunami::core::SemanticVersion implementation_version;
    std::string backend_description;
    OperationCapability validation;
    OperationCapability preparation;
    OperationCapability launch;
    OperationCapability cancellation;
    OperationCapability result_discovery;
    bool solver_available{};
};

} // namespace tsunami::application
