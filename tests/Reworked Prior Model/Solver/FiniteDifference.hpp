#pragma once
#include <vector>
/* 
    We 
*/
class FiniteDifference
{
protected:
    struct Step_Results
    {
        std::vector<std::vector<double>> vorticity; // vector field for vorticity
        std::vector<std::vector<double>> stream_function; // vector field for streamfunction
        std::vector<std::vector<double>> velocity; // vector field for velocity field
        std::vector<std::vector<double>> pressure; // vector field for pressure
        // Additional fields can be added but note that the velocity and pressure fields require manipulation of the vorticity and streamfunction fields and should be done so in functions. 
        double time;
        int step_number;
    };

    std::vector<int> Generate_Linspace(int start, int end, int length);

private:
    struct Spatial_Domain
    {
        int Lx;                                     // Number of grid points in x direction
        int Ly;                                     // Number of grid points in y direction
        double dx;                                  // Grid spacing in x direction
        double dy;                                  // Grid spacing in y direction
        int Nx;                                     // Number of grid points in x direction
        int Ny;                                     // Number of grid points in y direction
        std::vector<int> Lx_linspace;               // Linearly spaced x vector
        std::vector<int> Ly_linspace;               // Linearly spaced y vector
        std::vector<std::vector<int>> domain_space; // computational domain
    };

    struct Temporal_Domain
    {
        double dt; // Time step
        double T; // Final Time
        int Nt; // Number of key snap shots
    };

    Spatial_Domain Mesh_Generation();
    Step_Results Elliptic_Solver(Spatial_Domain, Step_Results);
    Step_Results Jacobian_Solver(Spatial_Domain, Step_Results);
    void Time_Stepping(Spatial_Domain, Step_Results);
    Step_Results Velocitiy_Field(Spatial_Domain, Step_Results);
    Step_Results Pressure_Field(Spatial_Domain, Step_Results);

public:
    // Setters
    void setDomain();
    // Getters
    Spatial_Domain getDomain();
    Step_Results getResults();
};

