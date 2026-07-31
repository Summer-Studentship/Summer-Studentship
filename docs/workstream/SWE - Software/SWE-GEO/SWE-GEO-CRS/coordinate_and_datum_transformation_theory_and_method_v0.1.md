# Coordinate and Datum Transformation Theory and Method

**Document ID:** `SWE-GEO-CRS-TM-v0.1`
**Work Package:** `SWE-GEO-CRS-WP1`
**Tasks:** `SWE-GEO-CRS-WP1-T1` to `SWE-GEO-CRS-WP1-T3`
**Status:** Proposed authoritative G1 methodology
**Scope:** Kamaishi and Sendai Regional2D corridor data
**Target implementation:** `tsunami_geo` and optional `tsunami_geo_proj`

---

## 1. Purpose

This document defines the theory and computational method used to transform imported geospatial data from their native coordinate and vertical reference systems into the common metric reference frame used by the Regional2D modelling workflow.

The method applies to:

* bathymetry;
* topography;
* earthquake displacement fields;
* prescribed free-surface perturbations;
* coastal and offshore observations;
* vector control features;
* corridor-defining geographic coordinates.

The method does not assume that datasets covering the same physical region use the same datum, coordinate epoch, vertical zero, axis order or sign convention.

The governing principle is:

```text
Native source data and declared reference
        ↓
Reference evidence and compatibility validation
        ↓
Authorised coordinate operation
        ↓
Explicit axis, unit and sign normalisation
        ↓
Metric computational coordinates
        ↓
Transformation provenance and uncertainty
```

The transformation process must never replace or erase the source reference. The native dataset and transformed dataset remain separate provenance records.

---

## 2. Reference-system concepts

### 2.1 Coordinate reference system

A coordinate reference system, or CRS, defines how coordinate values relate to positions in the physical world. A complete CRS may include:

* a datum or reference frame;
* an ellipsoid;
* a coordinate system;
* axis names and directions;
* angular or linear units;
* a map projection;
* an area of use;
* a coordinate epoch where the frame is dynamic or time-dependent.

A CRS identifier is not merely a file-format label. Two datasets containing numerically similar longitude and latitude values may represent different physical positions when their datums or coordinate epochs differ.

---

### 2.2 Geodetic datum and reference frame

A geodetic datum defines the relationship between a coordinate system and the Earth.

A horizontal position is therefore not fully specified by:

[
(\lambda,\phi),
]

where (\lambda) is longitude and (\phi) is latitude.

The position must instead be interpreted as:

[
\mathbf{p}
==========

(\lambda,\phi,\mathcal{D},t),
]

where:

* (\mathcal{D}) is the datum or reference-frame realisation;
* (t) is the coordinate epoch, where relevant.

This distinction is material for northern Honshu because the 11 March 2011 Tohoku earthquake caused substantial crustal deformation. GSI introduced JGD2011 after the earthquake to replace JGD2000 in affected areas. EPSG defines northern-Honshu JGD2011 as aligned to ITRF2008 at epoch 2011.395, whereas JGD2000 was aligned to ITRF94 at epoch 1997.0.

Consequently:

```text
same numerical latitude and longitude
    does not necessarily mean
same physical location
```

when the datum realisation or coordinate epoch differs.

---

### 2.3 Geographic and projected coordinates

Geographic coordinates express position using angular quantities:

[
(\lambda,\phi).
]

Projected coordinates map the curved reference surface onto a plane:

[
(E,N)
=====

\mathcal{P}_{\mathcal{D}}(\lambda,\phi),
]

where:

* (E) is easting;
* (N) is northing;
* (\mathcal{P}_{\mathcal{D}}) is the projection associated with datum (\mathcal{D}).

Projection and datum transformation are different operations.

A projection changes the coordinate representation while retaining the same datum:

[
(\lambda,\phi)*{\mathcal{D}}
\longrightarrow
(E,N)*{\mathcal{D}}.
]

A datum transformation changes the underlying reference frame:

[
(\lambda,\phi)_{\mathcal{D}*s}
\longrightarrow
(\lambda,\phi)*{\mathcal{D}_t}.
]

A source-to-computational transformation may require both.

---

## 3. Selected horizontal computational reference

### 3.1 Japanese Plane Rectangular Coordinate System Zone X

Both target regions lie within the official application area of Japan Plane Rectangular Coordinate System Zone X:

* Kamaishi is in Iwate Prefecture;
* Sendai is in Miyagi Prefecture.

GSI assigns Iwate and Miyagi, together with Aomori, Akita and Yamagata, to Zone X. The Zone X natural origin is:

[
\phi_0=40^\circ00'00''\mathrm{N},
]

[
\lambda_0=140^\circ50'00''\mathrm{E}.
]

The projection scale factor at the natural origin is:

[
k_0=0.9999.
]

False easting and false northing are both zero.

### 3.2 Project decision

The G1 horizontal target is:

```text
Name:
    JGD2011 / Japan Plane Rectangular CS X

Authority:
    EPSG

Code:
    6678

Unit:
    metre

Projection:
    Transverse Mercator
```

EPSG:6678 covers northern Honshu, including Iwate and Miyagi, and defines Cartesian axes as northing followed by easting.

Using one projected CRS for both corridors provides:

* one metric horizontal frame;
* no cross-zone transformation between the two case studies;
* consistent distance, width and area calculations;
* simpler corridor and terrain interfaces;
* directly comparable geometric quantities.

---

## 4. Axis convention

### 4.1 Authority axis order

EPSG:6678 defines:

```text
Axis 1:
    northing
    abbreviation X
    direction north

Axis 2:
    easting
    abbreviation Y
    direction east
```

This follows the Japanese plane-coordinate convention.

### 4.2 Project numerical convention

The project uses:

```text
x:
    eastward coordinate

y:
    northward coordinate

z:
    positive upward
```

Therefore:

[
x_{\mathrm{project}}
====================

Y_{\mathrm{JPRCS}},
]

[
y_{\mathrm{project}}
====================

X_{\mathrm{JPRCS}}.
]

This is an axis-storage mapping, not a geodetic transformation.

The transformation record must separately preserve:

```text
authority axis order:
    northing, easting

project storage order:
    easting, northing

mapping:
    project x ← authority easting
    project y ← authority northing
```

The implementation must not claim that the internal array order is the native EPSG axis order.

---

## 5. Vertical-reference theory

### 5.1 Ellipsoidal height

Ellipsoidal height (h) is measured relative to a reference ellipsoid.

It is a geometric quantity and is commonly produced by GNSS processing.

### 5.2 Gravity-related or orthometric height

Gravity-related height (H) is measured relative to a gravity-defined reference surface approximating mean sea level.

The relationship between ellipsoidal height, gravity-related height and geoid undulation is:

[
H=h-N,
]

where:

* (h) is ellipsoidal height;
* (H) is gravity-related height;
* (N) is geoid undulation.

A conversion between (h) and (H) therefore requires an accepted geoid model.

It cannot be performed by relabelling the height field.

### 5.3 Japanese national height reference

GSI defines Japanese land elevations relative to the mean sea level of Tokyo Bay. The Japanese levelling datum materialises this national height reference.

### 5.4 Project decision

The G1 target vertical CRS is:

```text
Name:
    JGD2011 (vertical) height

Authority:
    EPSG

Code:
    6695

Coordinate:
    gravity-related height

Unit:
    metre

Direction:
    positive up
```

EPSG:6695 defines a metre-based vertical coordinate whose axis is gravity-related height and whose positive direction is upward.

The horizontal and vertical reference components may be represented together by:

```text
EPSG:10171
JGD2011 / Japan Plane Rectangular CS X
    + JGD2011 (vertical) height
```

However, project memory and file contracts must still record the explicit axis remapping to:

[
(x,y,z)
=======

(\mathrm{easting},\mathrm{northing},\mathrm{height}).
]

The compound CRS definition alone does not describe the project storage order.

---

## 6. Bathymetric depth and vertical datum

Bathymetry is often supplied as depth (d), positive downward:

[
d>0
\quad\text{below the reference surface}.
]

The solver uses bed elevation (z_b), positive upward.

Where both quantities use the same vertical zero:

[
z_b=-d.
]

This sign conversion does not transform the vertical datum.

More generally, where the source and target zero surfaces differ:

[
z_{b,t}
=======

-d_s+\Delta_{s\rightarrow t},
]

where (\Delta_{s\rightarrow t}) is an authorised offset or spatially varying vertical transformation.

Therefore:

```text
positive-down → positive-up
    is a sign conversion

chart datum → national height datum
    is a datum transformation
```

The first operation does not authorise the second.

Unknown chart datum, unspecified mean sea level and unidentified gauge zero must not be treated as JGD2011 vertical height.

---

## 7. Historical-event treatment

The model reconstructs the 11 March 2011 event.

The transformation method must therefore retain:

* source datum;
* source datum realisation;
* coordinate epoch;
* source acquisition or survey date;
* whether the source represents pre-event or post-event geometry;
* target datum and epoch;
* earthquake correction operation, where applied.

The target reference cannot be selected solely because it is the newest available datum.

GSI introduced JGD2024 in 2025. GSI states that horizontal latitude, longitude and plane-coordinate values were inherited from JGD2011, while the principal revision concerned the national elevation system. GSI also notes that some current TIFF products may temporarily retain JGD2011 EPSG identifiers even when product metadata describes JGD2024.

Accordingly:

```text
JGD2024 horizontal metadata
    must be interpreted from the exact product documentation

JGD2024 vertical values
    must not be treated as JGD2011 vertical values

historical data
    must remain associated with its declared realisation and epoch
```

---

## 8. Horizontal transformation theory

### 8.1 General transformation

A horizontal transformation is represented as:

[
\mathbf{q}_t
============

\mathcal{T}_{h}
\left(
\mathbf{q}_s,
\mathcal{D}_s,
\mathcal{D}_t,
t_s,
t_t,
\mathcal{A},
\mathcal{G}
\right),
]

where:

* (\mathbf{q}_s) is the source coordinate;
* (\mathbf{q}_t) is the target coordinate;
* (\mathcal{D}_s) and (\mathcal{D}_t) are source and target reference frames;
* (t_s) and (t_t) are source and target epochs;
* (\mathcal{A}) is the area of interest;
* (\mathcal{G}) is the set of required transformation grids.

The transformation must be selected for the actual spatial extent of the source data.

### 8.2 JGD2000 to JGD2011

EPSG operation 6713 is:

```text
JGD2000 to JGD2011 (1)

Method:
    NTv2

Grid:
    touhokutaiheiyouoki2011.gsb

Registered accuracy:
    0.2 m

Purpose:
    correction for deformation caused by the
    2011 Tohoku earthquake
```

The method is:

[
(\lambda,\phi)*{\mathrm{JGD2000}}
\xrightarrow{\text{EPSG:6713}}
(\lambda,\phi)*{\mathrm{JGD2011}}
\xrightarrow{\text{EPSG:6678 projection}}
(E,N)_{\mathrm{JGD2011/X}}.
]

The required correction grid must be locally available.

An identity transformation, null shift or alternative lower-quality operation must not replace EPSG:6713 silently.

### 8.3 WGS84 to JGD2011

A source labelled `EPSG:4326` supplies a WGS84 geographic CRS but does not necessarily identify:

* the WGS84 realisation;
* the coordinate epoch;
* the survey epoch;
* the actual positional accuracy.

Generic WGS84 must therefore not be considered identical to JGD2011.

The selected operation must be compatible with:

* the dataset’s declared uncertainty;
* the model scale;
* the source epoch;
* the spatial area of use;
* available transformation resources.

Survey-grade data require a specific datum realisation and coordinate epoch or an authoritative Japanese datum declaration.

Regional grids with metre-scale native uncertainty may use a lower-accuracy WGS84-to-JGD2011 operation only when its stated operation accuracy is explicitly accepted in the uncertainty budget.

---

## 9. Vertical transformation methods

### 9.1 Identity transformation

An identity vertical operation is valid only when the source is explicitly declared as:

```text
JGD2011 vertical height
metres
positive up
```

Matching numeric units alone are insufficient.

### 9.2 Unit conversion

Where the vertical datum and sign convention are unchanged:

[
z_t=c_u z_s,
]

where (c_u) is an exact unit-conversion factor.

For example:

[
z_{\mathrm{m}}
==============

0.01z_{\mathrm{cm}}.
]

The source and target units must both be recorded.

### 9.3 Sign conversion

For a shared vertical zero:

[
z_{\mathrm{up}}
===============

-z_{\mathrm{down}}.
]

The provenance record must state:

```text
source direction:
    down

target direction:
    up

operation:
    sign inversion

datum zero:
    unchanged
```

### 9.4 Ellipsoidal-to-gravity-related conversion

Where a source contains JGD2011 ellipsoidal heights:

[
H_{\mathrm{JGD2011}}
====================

## h_{\mathrm{JGD2011}}

N_{\mathrm{GSI}},
]

where (N_{\mathrm{GSI}}) is obtained from an accepted GSI geoid model.

The operation is accepted only when:

* the source ellipsoidal CRS is explicit;
* the target vertical CRS is explicit;
* the required local geoid grid is installed;
* the grid identity is recorded;
* the grid checksum is recorded by the owning resource process;
* network fallback is disabled;
* no unrelated global geoid is substituted.

### 9.5 Authoritative constant offset

A constant vertical offset may be applied only where an authoritative product or station record declares:

* the source reference surface;
* the target reference surface;
* the offset value;
* the unit;
* the algebraic direction;
* the effective period;
* the uncertainty;
* the product or station identifier.

The operation is:

[
H_t=H_s+\Delta H_{s\rightarrow t}.
]

A user-estimated offset is not accepted as an authoritative datum conversion.

---

## 10. Unsupported G1 vertical operations

The following conversions are rejected unless a formally accepted operation or authoritative offset is supplied:

* unidentified chart datum to JGD2011 height;
* generic mean sea level to JGD2011 height;
* unknown tide-gauge zero to JGD2011 height;
* JGD2024 vertical height to JGD2011 vertical height;
* GEBCO assumed mean sea level to JGD2011 height;
* JGD2000 vertical height to JGD2011 vertical height without an accepted correction resource;
* ellipsoidal height to gravity-related height without the required geoid model.

A vertical transformation cannot be inferred from:

* the sign of offshore values;
* geographic location;
* the provider’s organisation;
* the fact that the data are called bathymetry;
* successful opening by GDAL or PROJ.

---

## 11. PROJ operation-selection method

### 11.1 Operation context

The transformation adapter must create a PROJ operation context using:

```text
area of interest:
    imported dataset or corridor extent

allow ballpark:
    false

only best:
    true

network access:
    disabled

desired accuracy:
    explicitly supplied

source epoch:
    supplied where known

target epoch:
    supplied where required
```

PROJ documents that `ALLOW_BALLPARK=NO` prevents approximate ballpark transformations and that `ONLY_BEST=YES` causes operation construction to fail when the best known operation cannot be instantiated, including cases where a required grid is unavailable. PROJ also exposes an operation-instantiability check for required resources.

### 11.2 Candidate operation filtering

The procedure is:

1. construct the source CRS;
2. construct the target CRS;
3. define the actual area of interest;
4. request candidate operations;
5. reject operations outside the area of use;
6. reject ballpark operations;
7. reject operations exceeding the accepted accuracy threshold;
8. reject operations requiring unavailable grids;
9. select the highest-ranked valid operation;
10. record the selected operation before transforming data.

No operation may be selected merely because it is the first returned candidate.

### 11.3 Required operation record

The transformation record must contain:

* source CRS in canonical WKT2 or PROJJSON;
* target CRS in canonical WKT2 or PROJJSON;
* source datum and realisation;
* target datum and realisation;
* source and target coordinate epochs;
* operation name;
* operation authority;
* operation code;
* operation accuracy;
* operation area of use;
* selected PROJ pipeline or PROJJSON operation;
* required resource grids;
* locally resolved grid paths or resource identifiers;
* PROJ version;
* axis normalisation;
* unit conversion;
* vertical sign conversion;
* warnings;
* rejection of ballpark fallback.

---

## 12. Combined computational method

For a source coordinate:

[
\mathbf{s}
==========

(\lambda_s,\phi_s,z_s),
]

the accepted transformation sequence is:

[
(\lambda_s,\phi_s,z_s)
\xrightarrow{\mathcal{T}_d}
(\lambda_t,\phi_t,z'*s)
\xrightarrow{\mathcal{P}*{X}}
(N,E,z'_s)
\xrightarrow{\mathcal{A}}
(E,N,z'_s)
\xrightarrow{\mathcal{T}_v}
(x,y,z_t),
]

where:

* (\mathcal{T}_d) is the horizontal datum transformation;
* (\mathcal{P}_X) is the Zone X Transverse Mercator projection;
* (\mathcal{A}) is the authority-to-project axis mapping;
* (\mathcal{T}_v) is the authorised vertical operation.

The final project coordinate is:

[
\mathbf{x}
==========

# (x,y,z)

(E,N,H),
]

with:

```text
x:
    eastward metres

y:
    northward metres

z:
    positive-up metres
```

Every stage may be identity, but no identity stage may be assumed without reference evidence.

---

## 13. Vector transformation method

For vector data, every geometry coordinate is transformed using the same validated operation:

[
\mathbf{x}_{t,i}
================

\mathcal{T}(\mathbf{x}_{s,i}),
\qquad
i=1,\ldots,N.
]

The method must preserve:

* feature identity;
* feature order;
* geometry type;
* ring structure;
* attribute schema;
* source attributes;
* source dataset identity.

The transformed geometry is staged separately.

The source vector object is not mutated.

If any coordinate fails transformation:

```text
complete transformed layer:
    not published

partial feature:
    not published

transformation record:
    not published
```

---

## 14. Raster transformation method

A raster is not transformed by changing only its corner coordinates.

Reprojection generally changes:

* grid orientation;
* pixel spacing;
* cell footprint;
* output dimensions;
* sampling positions.

It therefore requires resampling.

`SWE-GEO-CRS-WP1` will produce a validated raster transformation plan containing:

* source CRS;
* target CRS;
* source affine transform;
* transformed source footprint;
* candidate target extent;
* operation chain;
* datum operation;
* axis mapping;
* required resources;
* operation accuracy.

It will not generate a resampled raster.

Actual raster reprojection and interpolation belong to downstream terrain processing.

The source raster values and valid mask remain unchanged in this Work Package.

---

## 15. Transformation provenance

A transformed object is a new generated artefact.

The provenance graph is:

```text
native imported dataset
        ↓
coordinate/datum transformation process
        ↓
transformed generated dataset
```

The transformed object must not overwrite:

* the source asset;
* the source import record;
* the source dataset record;
* the source datum evidence.

The transformation process record must identify:

* input dataset ID;
* input asset ID;
* source import-record ID;
* output dataset ID;
* output artefact ID;
* case revision;
* manifest revision;
* operation timestamp;
* software and version;
* source CRS;
* target CRS;
* source and target epochs;
* horizontal operation;
* vertical operation;
* axis mapping;
* grid resources;
* operation accuracy;
* numerical residuals;
* warnings.

---

## 16. Accuracy and uncertainty

### 16.1 Distinct accuracy concepts

Three quantities must remain separate:

#### Source uncertainty

Uncertainty already present in the source data:

[
\sigma_s.
]

#### Transformation-operation accuracy

Accuracy declared for the geodetic operation:

[
\sigma_T.
]

For example, EPSG records an accuracy of (0.2\ \mathrm{m}) for operation 6713. This is the expected geodetic accuracy of the operation, not an acceptable programming residual.

#### Numerical implementation residual

Residual caused by finite precision or incorrect implementation:

[
\varepsilon_n.
]

### 16.2 Combined uncertainty

Where independence is a reasonable approximation, a transformed uncertainty estimate may be recorded as:

[
\sigma_{\mathrm{out}}
=====================

\sqrt{
\sigma_s^2
+
\sigma_T^2
+
\varepsilon_n^2
}.
]

Where independence is unsupported, use the conservative bound:

[
\sigma_{\mathrm{out}}
\leq
\sigma_s+\sigma_T+\lvert\varepsilon_n\rvert.
]

The selected combination method must be documented.

### 16.3 Acceptance principle

A transformation is scientifically admissible only when:

[
\sigma_T
\leq
\sigma_{\mathrm{accepted}},
]

where (\sigma_{\mathrm{accepted}}) is derived from:

* source-data resolution;
* source uncertainty;
* corridor scale;
* intended model use;
* validation requirements.

A transformation must not claim greater accuracy than its least accurate authoritative component.

---

## 17. Verification method

### 17.1 Projection-origin control

For Zone X:

[
\phi=\phi_0=40^\circ,
]

[
\lambda=\lambda_0=140^\circ50',
]

the authority coordinates are:

[
N=0,
\qquad
E=0.
]

The project coordinates must therefore be:

[
x=0,
\qquad
y=0.
]

This verifies:

* projection definition;
* natural origin;
* false-coordinate values;
* axis mapping.

### 17.2 Independent horizontal control points

Expected values must be obtained from an independent GSI source or committed official correction resource.

Each control must record:

* source coordinate;
* source datum;
* source epoch;
* expected target coordinate;
* target datum;
* operation or parameter resource;
* resource version;
* source document;
* access date.

Expected values must not be generated by the same project PROJ call being tested.

### 17.3 Independent vertical control points

Vertical controls must be obtained independently from:

* an official GSI geoid calculation;
* an official benchmark;
* an authoritative station conversion;
* a committed validated resource.

The expected vertical value must not be generated using the transformation implementation under test.

### 17.4 Round-trip tests

For a reversible transformation:

[
\mathbf{x}_s
\xrightarrow{\mathcal{T}}
\mathbf{x}_t
\xrightarrow{\mathcal{T}^{-1}}
\widehat{\mathbf{x}}_s.
]

The round-trip residual is:

[
r
=

\left|
\widehat{\mathbf{x}}_s-\mathbf{x}_s
\right|.
]

Round-trip agreement tests numerical reversibility.

It does not independently validate geodetic correctness because the same erroneous forward and inverse implementation may cancel.

### 17.5 Project verification tolerances

The following are implementation tolerances, not claims about source or transformation accuracy:

```text
Projection-only known-point residual:
    ≤ 0.001 m

Projection-only round-trip residual:
    ≤ 0.001 m

JGD2000-to-JGD2011 implementation agreement
against an independent control:
    ≤ 0.05 m

Vertical-grid implementation agreement
against an independent control:
    ≤ 0.02 m
```

These values may be tightened where the committed controls and installed transformation resources support stricter validation.

The authoritative operation accuracy must be recorded separately.

---

## 18. Failure policy

The operation must fail when:

* source CRS is missing;
* target CRS is missing;
* datum evidence is inferred or unknown;
* source and target epochs are materially required but unavailable;
* the required correction grid is unavailable;
* the selected operation is ballpark;
* the selected operation exceeds the accepted accuracy;
* the area of use does not cover the data;
* horizontal and vertical units conflict;
* vertical-positive conventions conflict;
* chart or gauge datum cannot be related to the target;
* a transformation produces nonfinite coordinates;
* the inverse control exceeds tolerance;
* source and target provenance cannot be recorded.

The implementation must not silently:

* select a weaker operation;
* use network-downloaded grids;
* substitute WGS84 for JGD2011;
* assume mean sea level;
* assume Tokyo Bay mean sea level;
* infer positive-up from value signs;
* overwrite source metadata.

---

## 19. Transactional publication

The transformation process is transactional.

The sequence is:

1. validate the request;
2. validate source datum evidence;
3. construct source and target CRS objects;
4. select and validate the operation;
5. verify required resources;
6. transform into staged storage;
7. validate transformed coordinates;
8. calculate diagnostics;
9. construct the transformation record;
10. construct the generated-dataset provenance update;
11. publish only after every stage succeeds.

On failure:

```text
source object:
    unchanged

target object:
    not published

transformation record:
    not published

manifest:
    unchanged
```

---

## 20. G1 method boundary

### Included

* JGD2011 Zone X target definition;
* source and target CRS validation;
* source and target epoch provenance;
* horizontal point transformation;
* vector-coordinate transformation;
* JGD2000-to-JGD2011 correction using the required grid;
* WGS84-to-JGD2011 operation selection;
* supported vertical identity, unit, sign, geoid and authoritative-offset operations;
* axis normalisation;
* resource validation;
* transformation records;
* independent known-point tests;
* round-trip tests;
* raster transformation planning.

### Excluded

* raster resampling;
* interpolation;
* target-raster grid construction;
* terrain merging;
* bathymetry/topography reconciliation;
* unknown chart-datum conversion;
* tide-model conversion;
* automatic JGD2024-to-JGD2011 vertical conversion;
* corridor geometry construction;
* terrain conditioning;
* mesh generation;
* solver preparation;
* GUI controls.

---

## 21. Method summary

The accepted G1 transformation method is:

```text
1. Preserve the imported native data.

2. Resolve source horizontal and vertical references from
   authoritative evidence.

3. Reject inferred, unknown or conflicting datum relationships.

4. Use JGD2011 / Japan Plane Rectangular CS X as the common
   horizontal reference for Kamaishi and Sendai.

5. Use JGD2011 vertical height as the positive-up vertical
   reference.

6. Select the best valid PROJ operation for the actual area of
   interest.

7. Disallow ballpark transformations and unavailable-grid
   fallbacks.

8. Record datum realisations, coordinate epochs, operation
   accuracy and grid resources.

9. Convert authority axes to project x=easting and y=northing
   explicitly.

10. Apply vertical conversion only when its zero-surface
    relationship is authoritative.

11. Validate against independent control points and round-trip
    residuals.

12. Publish transformed data as a new generated artefact with
    complete provenance.
```

---

## 22. Authoritative references

1. Geospatial Information Authority of Japan, **Plane Rectangular Coordinate System**, official Zone X origin and prefectural application area.

2. EPSG Geodetic Parameter Dataset, **JGD2011 / Japan Plane Rectangular CS X**, EPSG:6678.

3. EPSG Geodetic Parameter Dataset, **Japanese Geodetic Datum 2011**, datum EPSG:1128.

4. EPSG Geodetic Parameter Dataset, **JGD2000 to JGD2011 (1)**, transformation EPSG:6713.

5. EPSG Geodetic Parameter Dataset, **JGD2011 vertical height**, EPSG:6695.

6. EPSG Geodetic Parameter Dataset, **JGD2011 / Japan Plane Rectangular CS X + JGD2011 vertical height**, EPSG:10171.

7. Geospatial Information Authority of Japan, **Japanese geodetic systems and national height reference**.

8. PROJ documentation, **coordinate-operation selection, only-best behaviour, ballpark rejection and resource availability**.
