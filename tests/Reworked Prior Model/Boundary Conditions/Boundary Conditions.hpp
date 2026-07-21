#pragma once
#include <string>

class Boundary_Condition_Scenarios
{
public:
    // We allow the modifications of the boundary condition parameters
    struct Boundary_Condition_Velocities
    {
        double top_wall_velocity;
        double bottom_wall_velocity;
        double left_wall_velocity;
        double right_wall_velocity;
    };
    // Constructor
    Boundary_Condition_Scenarios(double v, double x, double y, double z){
        Boundary_Condition_Velocities velocities = {v, x, y, z};
    };

    std::string boundary_condition_scenario; // Periodic domain, Enclosed Driven Lid Cavity, Enclosed Shear Layer, Driven Channel.
private:
    // We only restrict modifications to the function which defines the boundary conditions
    void Boundary_Conditions(std::string boundary_condition_scenario, const Boundary_Condition_Velocities &params);
};