#pragma once
#include <string>

class Initial_Conditions
{

public:
    std::string Initial_Condition_Scenario; // Elliptic Gaussian, Taylor-Green Vortex

private:
    // Function to set the initial condition based on the chosen scenario
    void Initial_Condition(std::string Initial_Condition_Scenario);

};