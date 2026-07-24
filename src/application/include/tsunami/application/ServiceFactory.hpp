#pragma once

#include <memory>

#include <tsunami/application/ApplicationService.hpp>

namespace tsunami::application {

auto make_no_solver_application_service() -> std::unique_ptr<IApplicationService>;

} // namespace tsunami::application
