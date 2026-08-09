# R8 Regional2D Order Resolution

Primary numerical classification: `GLOBAL_FIRST_ORDER_VERIFIED`

MUSCL gate: `OPEN`

The R8 global MMS benchmark recovers first-order L1/L2 behavior on the finest pair even though the R7 semi-discrete local truncation diagnostic was sub-first-order. Geometry identities and exact-face-state quadrature are clean, so the reduced R7 local order is associated with the piecewise-constant/Rusanov local operator and structured cancellation in the global solve.

## Finest Global MMS Orders

- h/mass L1/L2/Linf: `0.894811`, `0.903766`, `0.943525`
- qx L1/L2/Linf: `0.940152`, `0.912929`, `0.690857`
- qy L1/L2/Linf: `0.990166`, `0.95736`, `0.64573`

## Temporal Check

Maximum L1/L2 relative change under dt halving: `2.99552e-06`.

## Build Environment

`cmake --build --preset linux-gcc-release-build` returned `1`.

vcpkg bootstrap/install fails during linux-gcc-release CMake regeneration and writes an empty vcpkg-bootstrap.log; vcpkg executable and bootstrap script are present, so no source-controlled root cause was identified without destructive build-cache cleanup.
