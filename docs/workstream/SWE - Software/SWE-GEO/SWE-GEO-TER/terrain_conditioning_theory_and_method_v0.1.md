# Terrain Conditioning Theory and Method v0.1

## Scope

This document governs `SWE-GEO-TER-WP1` and Tasks `#203` to `#205`.
The G1 output is a seamless positive-up terrain field `z_b = z_b(x,y)` on a
corridor-aligned, square, pixel-is-area target grid. The output uses metre
horizontal and vertical units, Float64 terrain values, and requires every active
corridor cell to have valid terrain or an explicit bounded fill lineage.

The method starts from imported bathymetry and topography, accepted raster
transformation plans, accepted transformation records, and a constructed
corridor. It does not choose production Kamaishi or Sendai terrain, extract a
coastline, smooth shorelines, generate meshes, write HDF5/XDMF, initialise
solvers or expose GUI visualisation.

## References

The horizontal target reference must match the constructed corridor target
reference. Source rasters must be pixel-is-area and their import records,
transformation plans and transformation records must match the case revision,
manifest revision, dataset ID, asset ID and target reference.

Imported source values are decoded exactly once:

```text
z0 = s v + o
```

where `s` is the band scale when present and `1` otherwise, and `o` is the
band offset when present and `0` otherwise. Vertical steps then run in declared
order: identity, unit scale, sign inversion, constant offset and at most one
geodetic grid operation.

## Target Grid

The target grid uses the corridor tangent `t = (t_x,t_y)` as increasing column
direction and the left normal `n = (n_x,n_y)` as decreasing row direction.
Columns increase in local `xi` from offshore to inland; rows decrease in local
`eta` from left side to right side.

Let:

```text
xi_min = xi_offshore
xi_max = xi_inland
L = xi_max - xi_min
Wmax = W
```

For requested spacing `h`:

```text
N_xi = ceil(L / h)
N_eta = ceil(Wmax / h)
N_xi >= 1
N_eta >= 1
N_xi N_eta <= Nmax
```

Padding is symmetric:

```text
p_xi = N_xi h - L
p_eta = N_eta h - Wmax
xi0 = xi_min - p_xi / 2
xi1 = xi_max + p_xi / 2
eta_top = Wmax / 2 + p_eta / 2
eta_bottom = -Wmax / 2 - p_eta / 2
```

The upper-left corner is:

```text
o = e + xi0 t + eta_top n
```

The affine transform is:

```text
origin_x        = o_x
pixel_width     = h t_x
row_rotation    = -h n_x
origin_y        = o_y
column_rotation = h t_y
pixel_height    = -h n_y
```

Cell centres are:

```text
xi_ij = xi0 + (i + 1/2) h
eta_ij = eta_top - (j + 1/2) h
x_ij = e + xi_ij t + eta_ij n
```

## Coverage

For every target cell:

```text
alpha_ij = A(cell_ij intersect C) / h^2
```

The implementation transforms the corridor ring into local coordinates, clips
the polygon against each axis-aligned local cell with Sutherland-Hodgman
clipping, computes shoelace area and clamps only tolerance-scale excursions
near zero or one.

Classification is:

```text
alpha = 0: outside_corridor
0 < alpha < alpha_min: excluded_boundary_fraction
alpha >= alpha_min: active
```

The G1 default accepted threshold is `alpha_min = 0.5`, but callers must provide
or explicitly accept it through policy.

## Resampling

The accepted transformation operation is reused; GDAL must not select a
different operation. Ballpark operations, missing grids, unverified grids and
network fallback are rejected. Identity transformation is permitted only for
matching source and target references.

The conservative upsampling ratio is:

```text
r_up = h_s,max / h_t
```

where `h_s,max` is the maximum transformed source spacing and `h_t` is the
target spacing. Bilinear interpolation is permitted for similar resolution and
bounded upsampling. Area-average resampling is required for material
downsampling. Nearest-neighbour elevation resampling is not exposed.

## Merge and Gaps

Bathymetry and topography are merged only through explicit priority. If both
sources are valid, the first-priority source is selected and the difference

```text
delta z_ij = z_topography,ij - z_bathymetry,ij
```

contributes to overlap diagnostics. Excess disagreement is rejected unless the
policy explicitly accepts priority with warning. Elevation sign is never used
for source selection and no coastal blending is performed.

Unresolved active cells are rejected by default. Bounded inverse-distance fill
may be used only for interior, bounded, same-lineage donor components:

```text
z(x) = sum(w_k z_k) / sum(w_k)
w_k = 1 / |x - x_k|^p
p > 0
```

Boundary-touching gaps, oversized components, mixed bathymetry/topography donor
families and excessive filled fraction are rejected.

## Lineage and Uncertainty

Every output cell records lineage: outside, excluded, selected bathymetry,
selected topography, overlap-selected variants, or filled-from-neighbourhood
variants. A successful terrain has zero unresolved active cells.

Uncertainty is not fabricated. Numeric output uncertainty is reported only when
all required source, transformation, resampling and fill components are
explicitly available and the combination policy is explicit. Otherwise the
status remains `not_reported`.

## Transactionality and Visuals

Terrain-conditioning records are canonical JSON with deterministic field order,
explicit nulls where represented, LF endings and one terminal newline. Record
and GeoTIFF writes are transactional through sibling temporary files.

G1 inspection artefacts are elevation, coverage and lineage GeoTIFFs. They are
verification outputs for QGIS or GDAL inspection, not final simulation
visualisation.

The durable G1 terrain handoff is the conditioned-terrain artefact bundle. Its
primary path is the `TerrainConditioningRecord` v2 output path; coverage and
lineage are deterministic siblings and are not recorded as new schema fields.
The lineage code vocabulary is method-neutral and versioned as
`terrain-cell-lineage-code-v1`; zero or unknown codes are invalid and never
default to `outside_corridor`.

Read-back is a domain validation step, not a source-data import. The adapter
checks bundle identity, role metadata, CRS semantics, vertical metadata,
grid/extent equivalence, band type/unit/description, absence of scale/offset and
nodata transforms, binary terrain mask, coverage classes and lineage categories.
Only a field-equivalent `ConditionedTerrainRaster` reconstructed from all three
artefacts may proceed to Regional2D geometry preflight and conservative
raster-to-cell transfer.
