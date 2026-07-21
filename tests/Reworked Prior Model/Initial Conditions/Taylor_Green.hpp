#pragma once 

class Taylor_Green
{
public:
    // We allow the modifications of the initial condition parameters
    struct Taylor_Green_Parameters
    {
        double amplitude; // amplitude of the Taylor-Green vortex
        double kx;        // wave number in x direction
        double ky;        // wave number in y direction
    };
    // Constructor
    Taylor_Green(double a, double kx, double ky)
    {
        Taylor_Green_Parameters parameters = {a, kx, ky};
    };

private:
    // We only restrict modifications to the function which defines the initial condition
    double Taylor_Green_Initial_Condition(double x, double y, const Taylor_Green_Parameters &params);
};
