#include <CLI/CLI.hpp>

int main(int argc, char** argv)
{
    CLI::App app{"Tsunami Barrier Simulation command-line scaffold"};
    app.set_version_flag("--version", "Tsunami Barrier Simulation 0.1.0");

    CLI11_PARSE(app, argc, argv);

    return 0;
}
