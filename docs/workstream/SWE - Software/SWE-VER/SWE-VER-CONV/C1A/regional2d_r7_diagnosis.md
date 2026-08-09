# R7 Exact Regional2D Spatial Benchmark Diagnosis

Study ID: `regional2d-spatial-upgrade-r7`

Final R7 classification: `BASELINE_ORDER_UNRESOLVED`

The exact manufactured semi-discrete benchmark did not recover first-order L1/L2 convergence for all Regional2D residual components. The first hard gate therefore remains closed and no limited-linear/MUSCL implementation was attempted.

## Benchmark

- Domain: `[0,1] x [0,1]`, flat bed, fully wet.
- State: `h=2.0+0.18 sin(2*pi*x) cos(2*pi*y)`, `qx=0.35+0.09 cos(2*pi*x) sin(4*pi*y)`, `qy=-0.22+0.07 sin(4*pi*x) cos(2*pi*y)`.
- Reference: cell-average `div(F(U*))` from high-order boundary quadrature.
- Norms: fixed interior window `0.2<=x,y<=0.8` at every refinement level.

## First-Order Gate

- baseline_first_order_verified: `False`
- failure_classification: `geometry/operator inconsistency`
- errors_monotonic_l1_l2: `True`
- reference_error_floor_remaining: `False`

Finest-pair L2 orders:

- mass: `0.202031`
- qx: `0.668968`
- qy: `0.774847`

## R6 Error Floor

R6 excluded time integration, and its finite-amplitude linearisation error scale was about `1e-4` of the linear qx residual scale, so neither is the material explanation. The material weakness was reference construction: centroid point states and a centroid linearised reference were used instead of cell averages and a nonlinear cell-average flux-divergence reference.

## Decision

Stop before MUSCL. The next scientific action is to inspect the cell-average-to-face-state operator consistency, including whether the residual path needs an exact first-order-consistent reconstruction/evaluation treatment before any higher-order extension is designed.
