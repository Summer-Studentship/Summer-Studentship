#include <CLI/CLI.hpp>

#include <iostream>

#include <tsunami/application/ServiceFactory.hpp>

int main(int argc, char** argv)
{
    CLI::App app{"Tsunami Barrier Simulation command-line scaffold"};
    app.set_version_flag("--version", "Tsunami Barrier Simulation 0.1.0");
    bool service_status = false;
    app.add_flag("--service-status", service_status, "Print shared application-service status");

    CLI11_PARSE(app, argc, argv);

    if (service_status) {
        const auto service = tsunami::application::make_no_solver_application_service();
        const auto description = service->describe();
        std::cout
            << "service_backend=" << description.implementation_id << '\n'
            << "service_boundary=available\n"
            << "solver_available=" << (description.solver_available ? "true" : "false") << '\n'
            << "validation_available=" << (description.validation.available ? "true" : "false") << '\n'
            << "preparation_available=" << (description.preparation.available ? "true" : "false") << '\n'
            << "launch_available=" << (description.launch.available ? "true" : "false") << '\n'
            << "cancellation_available=" << (description.cancellation.available ? "true" : "false") << '\n'
            << "result_discovery_available=" << (description.result_discovery.available ? "true" : "false") << '\n';
    }

    return 0;
}
