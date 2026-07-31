#include <tsunami/geo/ConstructedCorridor.hpp>

#include <utility>

namespace tsunami::geo
{
    ConstructedCorridor::ConstructedCorridor(
        Polygon2D polygon,
        BoundingBox2D extent,
        CorridorLocalBasis basis,
        CorridorLongitudinalStations stations,
        CorridorSpongeLimits sponge_limits,
        double offshore_width_m,
        double inland_width_m,
        double total_length_m,
        double area_m2,
        double perimeter_m)
        : polygon_{std::move(polygon)},
          extent_{extent},
          basis_{basis},
          stations_{stations},
          sponge_limits_{sponge_limits},
          offshore_width_m_{offshore_width_m},
          inland_width_m_{inland_width_m},
          total_length_m_{total_length_m},
          area_m2_{area_m2},
          perimeter_m_{perimeter_m}
    {
    }

} // namespace tsunami::geo
