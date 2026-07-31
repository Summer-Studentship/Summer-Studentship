# Corridor Construction Theory and Method v0.1

## Scope

`SWE-GEO-COR-WP1` constructs an evidence-driven, flat-ended horizontal corridor from two transformed reference points: an epicentre and a target. The method is implemented in `tsunami_geo`; it performs no coordinate transformation, raster clipping, terrain conditioning, mesh generation, boundary tagging, damping-coefficient generation, GDAL calls or PROJ calls.

The configured `regional_2d.corridor` fields are interpreted as constraints and dimensions:

- `origin`: transformed epicentre reference point.
- `bearing_degrees_clockwise_from_north`: consistency check against the evidence-derived bearing.
- `offshore_extent_m`: centreline length before the epicentre, `L_pre`.
- `inland_extent_m`: centreline length after the selected target, `L_inland`.
- `width_m`: offshore full width, `W`.
- `narrowing.inland_width_m`: target and inland full width, `W_i`.
- `sponge`: internal numerical region widths only.

## Coordinate Convention

The storage convention is `x` east, `y` north and `z` positive-up. Corridor geometry is horizontal: `z` is retained in reference-point evidence and excluded from the polygon.

## Evidence Points

The caller supplies borrowed `TransformedPointSet` and `CoordinateTransformationRecord` objects for the epicentre and target. The operation copies the selected coordinate, source CRS, target CRS, transformation identity, source dataset, source asset and document provenance into `CorridorReferencePointEvidence`. It retains no borrowed pointers.

Both points must share case revision, manifest revision, target CRS, storage axes, horizontal unit and coordinate epoch where present. The horizontal unit must be metres, and storage axes must be `east_north` or `east_north_up`.

## Centreline and Basis

Let `e = [x_e, y_e]` be the epicentre and `q = [x_q, y_q]` be the target. Then:

```text
d = q - e
D = ||d||_2
t = d / D
n = [-t_y, t_x]
```

`D` must exceed the configured minimum separation. `t` and `n` must be unit length, orthogonal and right-handed:

```text
||t|| = 1
||n|| = 1
t . n = 0
det[t,n] = 1
```

The derived bearing is:

```text
theta = atan2(t_x, t_y) * 180 / pi
theta = theta normalised to [0, 360)
```

The circular residual is:

```text
min(|theta_configured - theta|, 360 - |theta_configured - theta|)
```

The configured bearing must be within tolerance; it does not replace the evidence-derived basis.

## Local Coordinates

For a global point `p`, local corridor coordinates are:

```text
xi  = (p - e) . t
eta = (p - e) . n
```

The inverse is:

```text
p = e + xi t + eta n
```

Longitudinal stations are:

```text
offshore  = -L_pre
epicentre = 0
target    = D
inland    = D + L_inland
```

## Constant Width

For constant width, `w(xi)=W`. The offshore endpoint is `a=e-L_pre t` and the inland endpoint is `b=q+L_inland t`. The canonical closed counter-clockwise ring is:

```text
a_left, a_right, b_right, b_left, a_left
```

The area and perimeter checks are:

```text
A = W (L_pre + D + L_inland)
P = 2 (L_pre + D + L_inland) + 2 W
```

## Narrowing

Narrowing is linear from epicentre to target, constant offshore of the epicentre and constant inland of the target:

```text
w = W                  for -L_pre <= xi <= 0
w = W + (W_i-W) xi/D   for 0 < xi < D
w = W_i                for D <= xi <= D + L_inland
```

It requires `0 < W_i < W`. The canonical ring is:

```text
a_left, a_right, epicentre_right, target_right,
b_right, b_left, target_left, epicentre_left, a_left
```

Tolerance-equivalent consecutive duplicates are removed deterministically, for example when `L_inland=0`.

The analytic checks are:

```text
A = W L_pre + ((W + W_i) / 2) D + W_i L_inland
Delta_w = (W - W_i) / 2
P = W + W_i + 2 L_pre + 2 L_inland + 2 sqrt(D^2 + Delta_w^2)
```

## Sponge Regions

Sponge parameters are internal numerical limits. They never modify polygon coordinates, extent, area or perimeter.

```text
offshore: -L_pre <= xi <= -L_pre + S_o
side:     w(xi)/2 - S_s <= |eta| <= w(xi)/2
```

The limits require:

```text
0 <= S_o < L_pre + D + L_inland
0 <= 2 S_s < min_width
```

`min_width` is `W` for constant width and `W_i` for narrowing.

## Polygon Validation

The constructor creates exact closure by copying the first vertex as the last vertex. It validates finite coordinates, closure, at least four unique nonclosing vertices, positive edge length, positive signed area, simple nonself-intersecting exterior ring, flat end caps, bounding-box nondegeneracy, analytic area and analytic perimeter. Scale-aware comparison uses:

```text
|a-b| <= eps_abs + eps_rel max(1, |a|, |b|)
```

Segment intersection is deterministic for the small generated exterior ring. Adjacent edges and first/last edge adjacency are ignored; proper crossings, nonadjacent overlap and nonadjacent touching are rejected.

## Record and Handoff

The construction record schema is `tsunami.corridor_construction_record` version `1.0.0`, policy version `0.1`, formula version `flat-ended-epicentre-target-v1`. The default path is:

```text
manifests/corridors/<trajectory_id>.json
```

The record preserves the case revision, trajectory ID, configured field paths, policy tolerances, both transformation identities, source dataset and asset identities, target CRS, output dataset/process IDs, execution timestamp, local basis, stations, polygon, extent, area, perimeter and diagnostics.
