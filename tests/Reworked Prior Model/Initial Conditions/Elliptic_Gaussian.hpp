#pragma once 

#include <vector>

class Elliptic_Gaussian
{
public:
    // We allow the modifications of the initial condition parameters
    struct Elliptic_Gaussian_Parameters
    {
        double x0;             // x-coordinate of the center
        double y0;             // y-coordinate of the center
        double sigma_x;        // standard deviation in x direction
        double sigma_y;        // standard deviation in y direction
        double amplitude;      // amplitude of the Gaussian
        double rotation_angle; // rotation angle in degrees
    };

    // Constructor
    Elliptic_Gaussian(double x0, double y0, double sigma_x, double sigma_y, double amplitude, double rotation_angle)
    {
        Elliptic_Gaussian_Parameters parameters = {x0, y0, sigma_x, sigma_y, amplitude, rotation_angle};
        
    };

private:
    // We only restrict modifications to the function which defines the initial condition
    double Elliptic_Gaussian_Initial_Condition(std::vector<int> Lx, std::vector<int> Ly, const Elliptic_Gaussian_Parameters &params);
};