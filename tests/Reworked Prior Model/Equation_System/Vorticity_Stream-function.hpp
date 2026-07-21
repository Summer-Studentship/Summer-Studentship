#pragma once

/*
    So here we want to recreate what we did in the 3rd year project with a beeline towards phase 2 so we will only implement finite difference with boundary conditions and initial conditions like we did prior with the goal here changed to accomodate efficiency so GPU running, multithreading and so on. The data visualisation will be via mathplot hopefully or we will simply interface with matlab to handoff the results and rewrite the data visualisation in matlab. This serves as a practice for coding the real model and learn how to correctly and efficiently interface with other languages.
*/

class Vorticity_Streamfunction
{
public:
    // We allow the modifications of the parameters for vorticity and streamfunction calculation
    struct Vorticity_Streamfunction_Parameters
    {
        double nu;    // Kinematic Viscosity
        double omega; // Vorticity to be inherirted from the initial condition
    };

private:
    // We only restrict modifications to the function which defines the vorticity and streamfunction calculation
    void Vorticity_Transport();
    void Poisson_Equation();
};































// #pragma once
// #include <string>
// #include <vector>

// class Initial_Conditions
// {

// public:
//     std::string Initial_Condition_Scenario; // Elliptic Gaussian, Taylor-Green Vortex

// private:
//     // Function to set the initial condition based on the chosen scenario
//     void Initial_Condition(std::string Initial_Condition_Scenario);

//     class Elliptic_Gaussian
//     {
//     public:
//         // We allow the modifications of the initial condition parameters
//         struct Elliptic_Gaussian_Parameters
//         {
//             double x0;             // x-coordinate of the center
//             double y0;             // y-coordinate of the center
//             double sigma_x;        // standard deviation in x direction
//             double sigma_y;        // standard deviation in y direction
//             double amplitude;      // amplitude of the Gaussian
//             double rotation_angle; // rotation angle in degrees
//         };

//     private:
//         // We only restrict modifications to the function which defines the initial condition
//         double Elliptic_Gaussian_Initial_Condition(std::vector<int> Lx, std::vector<int> Ly, const Elliptic_Gaussian_Parameters &params);
//     };

//     class Taylor_Green
//     {
//     public:
//         // We allow the modifications of the initial condition parameters
//         struct Taylor_Green_Parameters
//         {
//             double amplitude; // amplitude of the Taylor-Green vortex
//             double kx;        // wave number in x direction
//             double ky;        // wave number in y direction
//         };

//     private:
//         // We only restrict modifications to the function which defines the initial condition
//         double Taylor_Green_Initial_Condition(double x, double y, const Taylor_Green_Parameters &params);
//     };
// };

// class Boundary_Condition_Scenarios
// {
// public:
//     // We allow the modifications of the boundary condition parameters
//     struct Boundary_Condition_Parameters
//     {
//         double top_wall_velocity;
//         double bottom_wall_velocity;
//         double left_wall_velocity;
//         double right_wall_velocity;
//     };

//     std::string boundary_condition_scenario; // Periodic domain, Enclosed Driven Lid Cavity, Enclosed Shear Layer, Driven Channel.
// private:
//     // We only restrict modifications to the function which defines the boundary conditions
//     void Boundary_Conditions(std::string boundary_condition_scenario, const Boundary_Condition_Parameters &params);

//     // We can further break down the boundary conditions into the specific scenarios as follows:

//     class Periodic_Domain
//     {
//     private:
//         void Periodic_Boundary_Case(std::string boundary_condition_scenario, const Boundary_Condition_Parameters &params) {
//             // Implement periodic boundary conditions for the shallow water model

//         };
//     };

//     class Enclosed_Driven_Lid_Cavity
//     {
//     private:
//         void Enclosed_Driven_Lid_Cavity_Boundary_Case(std::string boundary_condition_scenario, const Boundary_Condition_Parameters &params) {
//             // Implement enclosed driven lid cavity boundary conditions for the shallow water model

//         };
//     };

//     class Enclosed_Shear_Layer
//     {
//     private:
//         void Enclosed_Shear_Layer_Boundary_Case(std::string boundary_condition_scenario, const Boundary_Condition_Parameters &params) {
//             // Implement enclosed shear layer boundary conditions for the shallow water model

//         };
//     };

//     class Driven_Channel
//     {
//     private:
//         void Driven_Channel_Boundary_Case(std::string boundary_condition_scenario, const Boundary_Condition_Parameters &params) {
//             // Implement driven channel boundary conditions for the shallow water model

//         };
//     };
// };

