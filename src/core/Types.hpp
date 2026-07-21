/*
 */
#pragma once // Restrict multiple inclusions of this header file

#include <cstddef> // Include header for size_t

namespace tsunami::core
{
    using Real = double;       // Define a type alias for real numbers (double precision)
    using Index = std::size_t; // Define a type alias for indices (size_t)
    using Time = Real;         // Define a type alias for time (double precision)
    using Length = Real;       // Define a type alias for length (double precision)
    using Velocity = Real;     // Define a type alias for velocity (double precision)
}