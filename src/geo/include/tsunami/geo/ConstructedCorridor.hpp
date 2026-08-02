#pragma once

#include <vector>

#include <tsunami/core/Result.hpp>
#include <tsunami/geo/ImportedVector.hpp>

namespace tsunami::geo
{
    struct CorridorConstructionRecord;

    struct CorridorLocalBasis
    {
        Point2D tangent;
        Point2D left_normal;
        double epicentre_target_distance_m{};
        double derived_bearing_degrees_clockwise_from_north{};

        [[nodiscard]] auto operator==(const CorridorLocalBasis &) const -> bool = default;
    };

    struct CorridorLongitudinalStations
    {
        double offshore_xi_m{};
        double epicentre_xi_m{};
        double target_xi_m{};
        double inland_xi_m{};

        [[nodiscard]] auto operator==(const CorridorLongitudinalStations &) const -> bool = default;
    };

    struct CorridorSpongeLimits
    {
        double offshore_start_xi_m{};
        double offshore_end_xi_m{};
        double side_width_m{};
        double minimum_unsponge_width_m{};

        [[nodiscard]] auto operator==(const CorridorSpongeLimits &) const -> bool = default;
    };

    class ConstructedCorridor
    {
    public:
        ConstructedCorridor() = default;
        ConstructedCorridor(
            Polygon2D polygon,
            BoundingBox2D extent,
            CorridorLocalBasis basis,
            CorridorLongitudinalStations stations,
            CorridorSpongeLimits sponge_limits,
            double offshore_width_m,
            double inland_width_m,
            double total_length_m,
            double area_m2,
            double perimeter_m);

        [[nodiscard]] auto polygon() const noexcept -> const Polygon2D & { return polygon_; }
        [[nodiscard]] auto extent() const noexcept -> const BoundingBox2D & { return extent_; }
        [[nodiscard]] auto basis() const noexcept -> const CorridorLocalBasis & { return basis_; }
        [[nodiscard]] auto stations() const noexcept -> const CorridorLongitudinalStations & { return stations_; }
        [[nodiscard]] auto sponge_limits() const noexcept -> const CorridorSpongeLimits & { return sponge_limits_; }
        [[nodiscard]] auto offshore_width_m() const noexcept -> double { return offshore_width_m_; }
        [[nodiscard]] auto inland_width_m() const noexcept -> double { return inland_width_m_; }
        [[nodiscard]] auto total_length_m() const noexcept -> double { return total_length_m_; }
        [[nodiscard]] auto area_m2() const noexcept -> double { return area_m2_; }
        [[nodiscard]] auto perimeter_m() const noexcept -> double { return perimeter_m_; }

        [[nodiscard]] auto operator==(const ConstructedCorridor &) const -> bool = default;

    private:
        Polygon2D polygon_;
        BoundingBox2D extent_;
        CorridorLocalBasis basis_;
        CorridorLongitudinalStations stations_;
        CorridorSpongeLimits sponge_limits_;
        double offshore_width_m_{};
        double inland_width_m_{};
        double total_length_m_{};
        double area_m2_{};
        double perimeter_m_{};
    };

    [[nodiscard]] auto make_constructed_corridor_from_record(
        const CorridorConstructionRecord &record) -> tsunami::core::Result<ConstructedCorridor>;

} // namespace tsunami::geo
