# Regional2D Model Implementation Traceability Audit

Audit ID: `C1A-MODEL-IMPLEMENTATION-TRACEABILITY-001`

Model freeze classification: `MODEL_CONSISTENT_WITH_DOCUMENTATION_FIXES`

Decision: proceed to Regional2D numerical scheme improvement with frozen G6 terrain, physics, boundary roles, and one-way coupling. The audit found no `MISMATCH` or `NOT_IMPLEMENTED_REQUIRED` rows in the core acceptance areas. The remaining issues are documentation wording repairs.

## Classification Counts

| classification | count |
| --- | --- |
| DEFERRED | 5 |
| DISCRETE_EQUIVALENT | 25 |
| DOCUMENTATION_AMBIGUITY | 2 |
| EXACT_MATCH | 12 |
| IMPLEMENTATION_EXTENSION | 2 |
| MISMATCH | 0 |
| NOT_IMPLEMENTED_REQUIRED | 0 |
| OPTIONAL_DISABLED | 2 |

## Core Acceptance

| area | row_count | status | blocking_rows |
| --- | --- | --- | --- |
| governing_equations | 5 | clear | none |
| physical_source_model | 7 | clear | none |
| baseline_boundary_roles | 9 | clear | none |
| one_way_coupling | 6 | clear | none |

Blocking rows overall: `none`.

## Traceability Matrix

| id | scope | component | classification | finding | impact | action |
| --- | --- | --- | --- | --- | --- | --- |
| R2D-STATE-001 | Regional2D | Conserved state | EXACT_MATCH | The implementation uses the same conserved variables and wet-state primitive recovery. | No model-code issue. | Keep as baseline. |
| R2D-STATE-002 | Regional2D | Free surface | EXACT_MATCH | The sign convention and eta relation are preserved in state preparation, snapshots, and coupling export. | No model-code issue. | Keep eta=h+b explicit in future case evidence. |
| R2D-EQS-001 | Regional2D | Continuity equation | DISCRETE_EQUIVALENT | The continuous conservation law is implemented as a conservative cell residual. | No governing-equation mismatch. | Proceed with numerical method changes as discretisation work. |
| R2D-EQS-002 | Regional2D | Momentum equations | DISCRETE_EQUIVALENT | Momentum physics are represented through standard finite-volume operators rather than pointwise Cartesian derivatives. | No governing-equation mismatch. | Numerical-order work may target flux/reconstruction without changing the model. |
| R2D-FLUX-001 | Regional2D | Cartesian fluxes | DISCRETE_EQUIVALENT | The code evaluates F n_x + G n_y directly, which is the face-normal finite-volume equivalent. | No flux-model mismatch. | Keep flux changes restricted to numerical flux/reconstruction experiments. |
| R2D-FLUX-002 | Regional2D | Normal velocity and wave speed | EXACT_MATCH | The local Lax-Friedrichs wave speed matches the documented baseline. | No issue. | Keep as reference when testing alternative fluxes. |
| R2D-FLUX-003 | Regional2D | Rusanov numerical flux | EXACT_MATCH | The documented numerical flux is implemented algebraically. | No issue. | Retain Rusanov as the frozen reference for convergence comparisons. |
| R2D-SRC-BED-001 | Regional2D | Bathymetric source | DISCRETE_EQUIVALENT | The bed slope is not differenced as a raw gradient; it is represented by a well-balanced hydrostatic discretisation. | No physics mismatch; this is the accepted discrete equivalent. | Keep lake-at-rest tests as mandatory evidence for changes. |
| R2D-SRC-FRIC-001 | Regional2D | Manning friction | DISCRETE_EQUIVALENT | The update is a semi-implicit exact damping form for the same algebraic source direction. | No physical-source mismatch. | Document the semi-implicit source discretisation in numerical-method notes. |
| R2D-SRC-COR-001 | Regional2D | Coriolis forcing | OPTIONAL_DISABLED | The optional term is implemented but not active in accepted C1A/G6 baselines. | No blocker because the model marks Coriolis optional. | Keep disabled unless event/domain evidence requires it. |
| R2D-SRC-VISC-001 | Regional2D | Explicit lateral eddy viscosity | OPTIONAL_DISABLED | The source catalogue explicitly excludes lateral eddy viscosity from the baseline. | No source-model mismatch. | Do not conflate Rusanov numerical diffusion with a physical eddy-viscosity closure. |
| R2D-SRC-EQ-001 | Regional2D | Earthquake source wording | DOCUMENTATION_AMBIGUITY | There is a wording conflict between older RES-MOD source text and accepted G6/consolidated baseline evidence. | Documentation ambiguity only; accepted implementation decision is unambiguous. | Update RES-MOD-SRC to state instantaneous passive initial transfer is current baseline and dynamic moving bed is deferred. |
| R2D-SRC-EQ-002 | Regional2D | Passive free-surface transfer | EXACT_MATCH | The code implements the accepted passive transfer baseline. | No earthquake-source mismatch for the accepted baseline. | Keep dynamic rupture as a separate future feature, not a hidden model requirement. |
| R2D-SRC-EQ-003 | Regional2D | Post-event bed and initial state | EXACT_MATCH | The propagation model starts from the expected passive earthquake initial condition. | No blocker. | Keep source metadata in case outputs to prevent double-application. |
| R2D-BC-001 | Regional2D | Boundary roles | DISCRETE_EQUIVALENT | The baseline boundary roles are implemented through characteristic exterior states plus optional sponge residuals. | No baseline boundary-role mismatch. | Keep reflection tests tied to boundary policy changes. |
| R2D-BC-002 | Regional2D | Sponge/relaxation source | DISCRETE_EQUIVALENT | The sign convention is consistent with residual-form explicit updates. | No source-sign mismatch. | Keep profile parameters in case evidence. |
| R2D-BC-003 | Regional2D | Exact radiation/sponge selection | DOCUMENTATION_AMBIGUITY | The implementation is accepted, but the consolidated text still describes this as unresolved. | Documentation ambiguity only; not a mathematical mismatch. | Update model text to cite the accepted characteristic/open-ocean damped policies. |
| R2D-WD-001 | Regional2D | Wet/dry treatment | DISCRETE_EQUIVALENT | The required wet/dry behavior is represented by discrete positivity and canonicalisation rules. | No model mismatch. | Maintain shoreline regression tests before changing reconstruction. |
| R2D-RECON-001 | Regional2D | Piecewise-constant verification baseline | DISCRETE_EQUIVALENT | This is the accepted low-order baseline used by the frozen-terrain convergence evidence. | The R6 non-convergence diagnosis is a numerical-method limitation, not a model mismatch. | Treat higher-order reconstruction as a scheme improvement. |
| R2D-RECON-002 | Regional2D | Limited linear reconstruction | DEFERRED | The model text frames this as a later numerical upgrade, not a required physical model feature. | This is the intended next numerical scheme improvement area. | Proceed only as a numerical-discretisation change with frozen physics and terrain. |
| R2D-TIME-001 | Regional2D | Explicit SSP Runge-Kutta | EXACT_MATCH | The documented explicit time-integration family is implemented. | No model mismatch. | Temporal-convergence work remains separate from spatial scheme changes. |
| R2D-TIME-002 | Regional2D | CFL and positivity restrictions | DISCRETE_EQUIVALENT | The implementation uses the documented timestep controls in discrete form. | No blocker. | Preserve timestep policy when isolating spatial convergence. |
| R2D-TIME-003 | Regional2D | Accept/retry step sequence | IMPLEMENTATION_EXTENSION | The retry machinery is an implementation robustness mechanism around the explicit model. | No physics-model issue. | Keep retry diagnostics out of physical-parameter invariance checks. |
| R2D-PARAM-001 | Regional2D | Gravity | EXACT_MATCH | The accepted baseline parameter is explicitly configured. | No parameter mismatch. | Keep gravity frozen for spatial-method experiments. |
| R2D-PARAM-002 | Regional2D | Manning coefficient | DISCRETE_EQUIVALENT | Uniform roughness is a case-data simplification within the documented input space. | No model mismatch. | Do not vary roughness in frozen spatial-convergence studies. |
| CPL-EXPORT-001 | Coupling | Regional handoff variables | EXACT_MATCH | The export contract contains the required handoff variables. | No coupling-contract mismatch. | Keep contract_version increments for future schema changes. |
| CPL-MAP-001 | Coupling | Normal and tangential projection | EXACT_MATCH | The projection matches the one-way coupling operator. | No issue. | Retain projection diagnostics for replay regression tests. |
| CPL-MAP-002 | Coupling | Depth-uniform velocity lift | DISCRETE_EQUIVALENT | The implementation uses the documented simplest baseline and rescales to preserve discrete discharge over wet faces. | No coupling-model mismatch. | Document depth-uniform lift as accepted, not merely unresolved. |
| CPL-MAP-003 | Coupling | Free-surface alpha reconstruction | DISCRETE_EQUIVALENT | The discrete face-fraction mapping is the finite-volume equivalent of the Heaviside alpha rule. | No issue. | Keep alpha bounds tests for replay changes. |
| CPL-MAP-004 | Coupling | Discharge preservation | DISCRETE_EQUIVALENT | The implementation preserves discharge in the discrete support/face quadrature used by the local inlet. | No issue. | Use residual diagnostics as replay acceptance evidence. |
| CPL-ONEWAY-001 | Coupling | One-way replay | EXACT_MATCH | The implementation matches the accepted one-way coupling assumption. | No one-way-coupling mismatch. | Keep two-way coupling deferred unless reflection evidence requires it. |
| L3D-EQS-001 | Local3D | Incompressible two-phase URANS | DISCRETE_EQUIVALENT | The repository configures an adopted solver backend for the documented equations. | No local governing-equation mismatch. | Keep backend/version authority records with future OpenFOAM changes. |
| L3D-VOF-001 | Local3D | VOF phase transport | DISCRETE_EQUIVALENT | The adopted backend implements bounded finite-volume VOF transport. | No issue. | Keep alpha boundedness acceptance checks. |
| L3D-VOF-002 | Local3D | Interface compression | IMPLEMENTATION_EXTENSION | Compression is a documented numerical extension of the physical alpha advection equation. | No physical-model mismatch. | Do not interpret compression as an additional physical phase source. |
| L3D-SST-001 | Local3D | k-omega SST turbulence | EXACT_MATCH | The configured turbulence model matches the selected local closure. | No issue. | Keep LES comparison deferred. |
| L3D-MAT-001 | Local3D | Water-air mixture properties | DISCRETE_EQUIVALENT | The backend receives the required two-phase material-property configuration. | No issue. | Record any future material calibration separately. |
| L3D-BC-IN-001 | Local3D | Coupling inlet | DISCRETE_EQUIVALENT | The local inlet receives the mapped regional time series through OpenFOAM native boundaryData. | No baseline boundary mismatch. | Keep one-way inlet as immutable when testing Regional2D scheme changes. |
| L3D-BC-OUT-001 | Local3D | Outlet boundary | DISCRETE_EQUIVALENT | The selected OpenFOAM open-ocean policy implements the intended outlet role. | No issue. | Keep reflection coefficients in boundary-policy evidence. |
| L3D-BC-SIDE-001 | Local3D | Lateral boundary | DISCRETE_EQUIVALENT | The accepted production policy matches the documented physical role. | No issue. | Do not regress production laterals to rigid or symmetry-only behavior. |
| L3D-BC-TOP-001 | Local3D | Atmosphere boundary | DISCRETE_EQUIVALENT | The OpenFOAM boundary choices implement the intended open atmospheric role. | No issue. | Keep top boundary policy in generated boundary_policy.json. |
| L3D-BC-TERRAIN-001 | Local3D | Terrain wall | DISCRETE_EQUIVALENT | The OpenFOAM wall policy implements fixed terrain with wall-shear closure. | No issue. | Keep wall-function evidence with production Local3D runs. |
| L3D-BC-BARRIER-001 | Local3D | Rigid barrier wall | DISCRETE_EQUIVALENT | The rigid-wall local model is implemented through OpenFOAM wall patches and forces extraction. | No issue. | Keep deformable/porous barriers out of current baseline claims. |
| L3D-FRC-001 | Local3D | Force and moment extraction | DISCRETE_EQUIVALENT | Integrated force/moment extraction is available, while full load convergence remains a verification task. | No model mismatch; downstream validation remains open. | Do not require full force convergence before Regional2D spatial-scheme work. |
| L3D-TIME-001 | Local3D | Adaptive timestep/CFL control | DISCRETE_EQUIVALENT | The local solver timestep policy is implemented through backend configuration and repository evidence. | No blocker for Regional2D spatial work. | Keep formal Local3D timestep convergence deferred. |
| HLD-BIDIR-001 | Hybrid | Two-way coupling | DEFERRED | The model explicitly accepts one-way replay as the current baseline. | No current-baseline mismatch. | Revisit only if reflection/placement evidence invalidates one-way replay. |
| HLD-LES-001 | Hybrid | Production LES | DEFERRED | LES is outside the G6/C1A accepted baseline. | No blocker. | Keep LES in future verification/validation planning. |
| HLD-FSI-001 | Hybrid | Fluid-structure interaction and structural damage | DEFERRED | These features are excluded from the accepted theoretical-model gate. | No blocker for numerical scheme work. | Keep excluded features out of model-consistency acceptance criteria. |
| HLD-DISP-001 | Hybrid | Regional dispersive correction | DEFERRED | Dispersive physics are a future model extension, not a missing required term. | No current-baseline mismatch. | Do not introduce dispersive terms as part of spatial-order repairs. |

## Assumptions

| id | assumption | basis | impact |
| --- | --- | --- | --- |
| ASM-001 | The accepted model authority is the consolidated model plus G6 traceability/gate evidence where newer than fragmented RES-MOD wording. | G6 accepted traceability explicitly records passive earthquake transfer, one-way replay, and Local3D policies. | Older RES-MOD wording can be a documentation ambiguity without blocking numerical scheme work. |
| ASM-002 | C1A frozen-terrain spatial studies keep terrain, earthquake source, Manning, Coriolis, boundary policy, and replay contracts fixed. | The audit is a model-to-code traceability check before numerical scheme changes. | Observed C1A spatial behavior is attributed to discretisation unless a listed core mismatch appears. |
| ASM-003 | OpenFOAM Foundation 11 is treated as an adopted implementation authority for the local incompressibleVoF/SST equations. | Existing G6 authority records and replay tests exercise generated dictionaries rather than reimplementing URANS/VOF in repository code. | Traceability cites generated configuration and authority evidence for Local3D operators. |
| ASM-004 | The current Regional2D earthquake baseline is instantaneous initialization, not finite-rise-time moving-bed forcing. | Consolidated model text and G6 traceability select passive free-surface transfer. | Dynamic rupture is classified as deferred/documentation repair, not a blocking implementation mismatch. |
| ASM-005 | Uniform Manning n=0.025 is a case-data decision for the accepted Kamaishi/frozen-terrain baselines. | Spatially varying roughness classes are noted as unresolved model inputs. | Roughness variation is not part of the numerical-convergence isolation. |
| ASM-006 | The Local3D production lateral/open-ocean policy supersedes the legacy synthetic schema 1.0.0 symmetry fixture. | G6 traceability records schema 1.1.0 production open_ocean_damped policy and reflection metrics. | Legacy symmetry tests remain compatibility tests, not the current physical boundary baseline. |

## Deferred Features

| id | feature | reason | classification |
| --- | --- | --- | --- |
| DEF-001 | Limited linear/MUSCL Regional2D reconstruction | Numerical scheme improvement target after P0 frozen-physics diagnosis. | DEFERRED |
| DEF-002 | Dynamic moving-bed earthquake rupture | Accepted baseline uses instantaneous passive free-surface transfer. | DEFERRED |
| DEF-003 | Two-way Regional2D-Local3D coupling | One-way replay is accepted unless reflection materially changes the incident field. | DEFERRED |
| DEF-004 | Production LES | URANS k-omega SST is the operational Local3D baseline. | DEFERRED |
| DEF-005 | FSI, structural damage, deformable barriers, scour | Excluded from the G6 theoretical-model gate. | DEFERRED |
| DEF-006 | Observation calibration and validation | Explicitly outside G6 and C1A numerical convergence scope. | DEFERRED |
| DEF-007 | Dispersive regional propagation | Current regional model is NLSWE. | DEFERRED |
| DEF-008 | Surrogate/ML optimisation and full impact metric suite | Later workstream outside current model-code consistency gate. | DEFERRED |

## Numerical vs Model Choices

| id | choice | category | classification | decision |
| --- | --- | --- | --- | --- |
| NUM-001 | Rusanov flux | numerical discretisation | EXACT_MATCH | Retained as robust reference flux; alternatives such as HLL/HLLC are controlled future scheme tests. |
| NUM-002 | Hydrostatic reconstruction | numerical discretisation of bed source | DISCRETE_EQUIVALENT | Accepted discrete equivalent of the continuous bed-slope source and well-balancing requirement. |
| NUM-003 | Piecewise-constant state reconstruction | numerical discretisation | DISCRETE_EQUIVALENT | Current P0 baseline; R6 evidence points to numerical-order limits, not missing physics. |
| NUM-004 | Limited linear/MUSCL reconstruction | numerical discretisation | DEFERRED | Legitimate next C1A improvement if physics/terrain remain frozen. |
| NUM-005 | Manning semi-implicit split update | numerical source integration | DISCRETE_EQUIVALENT | Implements the same damping source direction with a stable update. |
| NUM-006 | VOF interface compression | Local3D numerical discretisation | IMPLEMENTATION_EXTENSION | Backend numerical mechanism for bounded interface capturing, not extra physical forcing. |
| NUM-007 | One-way replay | coupling architecture | EXACT_MATCH | Accepted hybrid baseline; two-way feedback is a deferred model extension. |

## Documentation Conflicts

| id | topic | classification | implemented reality | recommended resolution | blocking |
| --- | --- | --- | --- | --- | --- |
| DOC-CONFLICT-001 | Earthquake source formulation | DOCUMENTATION_AMBIGUITY | RegionalEarthquakeInitialisation computes effective displacement, builds post-event bed/depth, and zeroes post-event momentum at initialization. | Revise RES-MOD-SRC to state instantaneous passive initial transfer is the current accepted baseline; mark dynamic moving-bed rupture as deferred. | False |
| DOC-CONFLICT-002 | Radiation/sponge policy selection | DOCUMENTATION_AMBIGUITY | Regional2D characteristic radiation and relaxation zones exist; Local3D production replay uses open_ocean_damped with isotropicDamping. | Update consolidated/RES-MOD boundary wording to cite accepted G6 boundary policy and leave only parameter tuning as future evidence. | False |

## Authority Documents

- `docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex`
- `docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/*`
- `docs/workstream/wbs-reconciliation/g6_theoretical_model_gate.md`
- `docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md`
- `docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.csv`

## Source Areas Inspected

- Regional2D state, flux, hydrostatic reconstruction, residual, sources, wet/dry, boundaries, relaxation, timestep, case runner, and CSV/coupling export.
- Coupling section export and OpenFOAM replay boundaryData conversion.
- Local3D OpenFOAM replay dictionaries, turbulence/wall/damping/timestep policies, boundary reflection evidence, and synthetic smoke evidence.
