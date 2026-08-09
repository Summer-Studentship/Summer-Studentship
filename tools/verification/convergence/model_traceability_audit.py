#!/usr/bin/env python3
"""Generate the C1A Regional2D/Local3D model implementation traceability audit."""

from __future__ import annotations

import csv
import json
from collections import Counter
from dataclasses import dataclass, asdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
OUTPUT_DIR = ROOT / "docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A"
JSON_PATH = OUTPUT_DIR / "regional2d_model_implementation_traceability.json"
CSV_PATH = OUTPUT_DIR / "regional2d_model_implementation_traceability.csv"
MD_PATH = OUTPUT_DIR / "regional2d_model_implementation_traceability.md"

CLASSIFICATIONS = {
    "EXACT_MATCH",
    "DISCRETE_EQUIVALENT",
    "OPTIONAL_DISABLED",
    "DEFERRED",
    "IMPLEMENTATION_EXTENSION",
    "DOCUMENTATION_AMBIGUITY",
    "MISMATCH",
    "NOT_IMPLEMENTED_REQUIRED",
}


@dataclass(frozen=True)
class TraceRow:
    requirement_id: str
    scope: str
    component: str
    model_requirement: str
    implementation_route: str
    classification: str
    finding: str
    evidence: list[str]
    impact: str
    recommended_action: str
    blocking_for_numerical_scheme_work: bool = False


def row(
    requirement_id: str,
    scope: str,
    component: str,
    model_requirement: str,
    implementation_route: str,
    classification: str,
    finding: str,
    evidence: list[str],
    impact: str,
    recommended_action: str,
    blocking: bool = False,
) -> TraceRow:
    if classification not in CLASSIFICATIONS:
        raise ValueError(f"{requirement_id}: unsupported classification {classification}")
    return TraceRow(
        requirement_id,
        scope,
        component,
        model_requirement,
        implementation_route,
        classification,
        finding,
        evidence,
        impact,
        recommended_action,
        blocking,
    )


TRACE_ROWS = [
    row(
        "R2D-STATE-001",
        "Regional2D",
        "Conserved state",
        "The regional NLSWE state is U=[h,qx,qy]^T with q=h u.",
        "RegionalConservedState and ShallowWaterState store depth, momentum_x, momentum_y.",
        "EXACT_MATCH",
        "The implementation uses the same conserved variables and wet-state primitive recovery.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:280",
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:292",
            "src/r2d/src/RegionalConservedState.cpp",
            "src/r2d/src/ShallowWaterState.cpp",
        ],
        "No model-code issue.",
        "Keep as baseline.",
    ),
    row(
        "R2D-STATE-002",
        "Regional2D",
        "Free surface",
        "Free-surface elevation is eta=h+b with bed elevation positive upward.",
        "FreeSurfaceElevation and snapshot/export writers compute/store bed_elevation and free_surface_elevation.",
        "EXACT_MATCH",
        "The sign convention and eta relation are preserved in state preparation, snapshots, and coupling export.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:280",
            "src/r2d/src/FreeSurfaceElevation.cpp",
            "src/r2d_io/src/RegionalCsvOutputWriter.cpp:328",
            "src/r2d_case_runner/src/RegionalFileCaseRunner.cpp:1074",
        ],
        "No model-code issue.",
        "Keep eta=h+b explicit in future case evidence.",
    ),
    row(
        "R2D-EQS-001",
        "Regional2D",
        "Continuity equation",
        "dh/dt + div(q)=source-free mass evolution except boundary/relaxation terms.",
        "Well-balanced finite-volume residual updates depth by net face mass flux and relaxation source where configured.",
        "DISCRETE_EQUIVALENT",
        "The continuous conservation law is implemented as a conservative cell residual.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:302",
            "src/r2d/src/WellBalancedResidualEvaluation.cpp",
            "src/r2d/src/WetDryUpdate.cpp",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:5",
        ],
        "No governing-equation mismatch.",
        "Proceed with numerical method changes as discretisation work.",
    ),
    row(
        "R2D-EQS-002",
        "Regional2D",
        "Momentum equations",
        "dqx/dt and dqy/dt use advective momentum flux, hydrostatic pressure, bed slope, friction, optional Coriolis, and relaxation.",
        "Normal flux, hydrostatic reconstruction, local sources, and relaxation residual jointly implement the momentum balance.",
        "DISCRETE_EQUIVALENT",
        "Momentum physics are represented through standard finite-volume operators rather than pointwise Cartesian derivatives.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:302",
            "src/r2d/src/ShallowWaterFlux.cpp:76",
            "src/r2d/src/HydrostaticReconstruction.cpp:31",
            "src/r2d/src/RegionalSourceUpdate.cpp:117",
            "src/r2d/src/RegionalRelaxationZone.cpp",
        ],
        "No governing-equation mismatch.",
        "Numerical-order work may target flux/reconstruction without changing the model.",
    ),
    row(
        "R2D-FLUX-001",
        "Regional2D",
        "Cartesian fluxes",
        "F(U) and G(U) contain qx, qy, qx^2/h, qx*qy/h, qy^2/h and 0.5*g*h^2 pressure terms.",
        "ShallowWaterFlux evaluates the physical normal flux from U and the unit face normal.",
        "DISCRETE_EQUIVALENT",
        "The code evaluates F n_x + G n_y directly, which is the face-normal finite-volume equivalent.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:310",
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:452",
            "src/r2d/src/ShallowWaterFlux.cpp:105",
        ],
        "No flux-model mismatch.",
        "Keep flux changes restricted to numerical flux/reconstruction experiments.",
    ),
    row(
        "R2D-FLUX-002",
        "Regional2D",
        "Normal velocity and wave speed",
        "The Rusanov speed is max(|u_n|+sqrt(g h)) over left/right states.",
        "characteristic_signal_speed and maximum_characteristic_signal_speed use the same expression.",
        "EXACT_MATCH",
        "The local Lax-Friedrichs wave speed matches the documented baseline.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:448",
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:468",
            "src/r2d/src/ShallowWaterFlux.cpp:130",
        ],
        "No issue.",
        "Keep as reference when testing alternative fluxes.",
    ),
    row(
        "R2D-FLUX-003",
        "Regional2D",
        "Rusanov numerical flux",
        "The baseline numerical flux is local Lax-Friedrichs/Rusanov.",
        "RusanovFlux computes half-sum physical flux minus half alpha state jump.",
        "EXACT_MATCH",
        "The documented numerical flux is implemented algebraically.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:461",
            "src/r2d/src/RusanovFlux.cpp:15",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:6",
        ],
        "No issue.",
        "Retain Rusanov as the frozen reference for convergence comparisons.",
    ),
    row(
        "R2D-SRC-BED-001",
        "Regional2D",
        "Bathymetric source",
        "Bed source is S_b=[0,-g h b_x,-g h b_y]^T.",
        "Hydrostatic reconstruction uses b_f=max(b_L,b_R) and pressure corrections in the residual.",
        "DISCRETE_EQUIVALENT",
        "The bed slope is not differenced as a raw gradient; it is represented by a well-balanced hydrostatic discretisation.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:326",
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:477",
            "src/r2d/src/HydrostaticReconstruction.cpp:78",
            "src/r2d/src/WellBalancedResidualEvaluation.cpp",
        ],
        "No physics mismatch; this is the accepted discrete equivalent.",
        "Keep lake-at-rest tests as mandatory evidence for changes.",
    ),
    row(
        "R2D-SRC-FRIC-001",
        "Regional2D",
        "Manning friction",
        "Manning bottom friction damps qx and qy as -g n_M^2 q |q| / h^(7/3) in wet cells.",
        "RegionalSourceUpdate computes k=g*n^2/h^(7/3), rate=k*|q|, and scales momentum by 1/(1+k|q|dt).",
        "DISCRETE_EQUIVALENT",
        "The update is a semi-implicit exact damping form for the same algebraic source direction.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:336",
            "src/r2d/src/RegionalSourceTerms.cpp",
            "src/r2d/src/RegionalSourceUpdate.cpp:162",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:9",
        ],
        "No physical-source mismatch.",
        "Document the semi-implicit source discretisation in numerical-method notes.",
    ),
    row(
        "R2D-SRC-COR-001",
        "Regional2D",
        "Coriolis forcing",
        "Coriolis may be included for sufficiently long propagation scales.",
        "Coriolis source fields and rotation update exist, but the Kamaishi/frozen-terrain baseline disables them.",
        "OPTIONAL_DISABLED",
        "The optional term is implemented but not active in accepted C1A/G6 baselines.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:350",
            "src/r2d/src/RegionalSourceTerms.cpp",
            "src/r2d/src/RegionalSourceUpdate.cpp:177",
            "cases/kamaishi_delivery/case_spec.json:107",
        ],
        "No blocker because the model marks Coriolis optional.",
        "Keep disabled unless event/domain evidence requires it.",
    ),
    row(
        "R2D-SRC-VISC-001",
        "Regional2D",
        "Explicit lateral eddy viscosity",
        "Lateral eddy viscosity is not part of the baseline regional source model.",
        "No explicit lateral viscosity source is active; numerical diffusion comes only from the flux.",
        "OPTIONAL_DISABLED",
        "The source catalogue explicitly excludes lateral eddy viscosity from the baseline.",
        [
            "docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/RES-MOD-SRC - Physical Source Terms and Constitutive Closures/res-mod-src.tex:409",
            "src/r2d/src/RusanovFlux.cpp:15",
        ],
        "No source-model mismatch.",
        "Do not conflate Rusanov numerical diffusion with a physical eddy-viscosity closure.",
    ),
    row(
        "R2D-SRC-EQ-001",
        "Regional2D",
        "Earthquake source wording",
        "Some RES-MOD text names dynamic moving-bed earthquake bathymetry, while the consolidated selected baseline permits static passive transfer.",
        "The implementation applies effective displacement during initialization and then advances a fixed post-event bathymetry.",
        "DOCUMENTATION_AMBIGUITY",
        "There is a wording conflict between older RES-MOD source text and accepted G6/consolidated baseline evidence.",
        [
            "docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/RES-MOD-SRC - Physical Source Terms and Constitutive Closures/res-mod-src.tex:495",
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:362",
            "src/r2d/src/RegionalEarthquakeInitialisation.cpp:550",
            "docs/workstream/wbs-reconciliation/g6_theoretical_model_gate.md:22",
        ],
        "Documentation ambiguity only; accepted implementation decision is unambiguous.",
        "Update RES-MOD-SRC to state instantaneous passive initial transfer is current baseline and dynamic moving bed is deferred.",
    ),
    row(
        "R2D-SRC-EQ-002",
        "Regional2D",
        "Passive free-surface transfer",
        "The simplest selected baseline maps finite-fault-derived vertical seabed displacement to eta(x,y,0).",
        "surface_perturbation copies effective_bed_displacement unless a prescribed surface is supplied.",
        "EXACT_MATCH",
        "The code implements the accepted passive transfer baseline.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:364",
            "src/r2d/src/RegionalEarthquakeInitialisation.cpp:550",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:13",
        ],
        "No earthquake-source mismatch for the accepted baseline.",
        "Keep dynamic rupture as a separate future feature, not a hidden model requirement.",
    ),
    row(
        "R2D-SRC-EQ-003",
        "Regional2D",
        "Post-event bed and initial state",
        "After source initialization, terrain is fixed for propagation; q starts from zero.",
        "The initializer constructs post-event bathymetry, post-event depth, and zero post_qx/post_qy.",
        "EXACT_MATCH",
        "The propagation model starts from the expected passive earthquake initial condition.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:366",
            "src/r2d/src/RegionalEarthquakeInitialisation.cpp:566",
            "src/r2d/src/RegionalEarthquakeInitialisation.cpp:630",
        ],
        "No blocker.",
        "Keep source metadata in case outputs to prevent double-application.",
    ),
    row(
        "R2D-BC-001",
        "Regional2D",
        "Boundary roles",
        "Offshore/source, inland/outflow, and artificial lateral boundaries should be open/radiation; laterals must not be rigid coastal walls.",
        "Regional boundary overrides provide transmissive and characteristic radiation states; relaxation zones can damp artificial boundaries.",
        "DISCRETE_EQUIVALENT",
        "The baseline boundary roles are implemented through characteristic exterior states plus optional sponge residuals.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:373",
            "src/r2d/src/RegionalBoundaryCondition.cpp:309",
            "src/r2d/src/RegionalRelaxationZone.cpp",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:11",
        ],
        "No baseline boundary-role mismatch.",
        "Keep reflection tests tied to boundary policy changes.",
    ),
    row(
        "R2D-BC-002",
        "Regional2D",
        "Sponge/relaxation source",
        "S_r=-sigma(x)(U-U_ref), with sigma zero in the interior and increasing through the sponge.",
        "RegionalRelaxationZone adds area*rate*(current-reference) to the residual, yielding damping in the update.",
        "DISCRETE_EQUIVALENT",
        "The sign convention is consistent with residual-form explicit updates.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:375",
            "src/r2d/src/RegionalRelaxationZone.cpp",
        ],
        "No source-sign mismatch.",
        "Keep profile parameters in case evidence.",
    ),
    row(
        "R2D-BC-003",
        "Regional2D",
        "Exact radiation/sponge selection",
        "The consolidated model leaves exact radiation condition and sponge profile unresolved pending reflection tests.",
        "Characteristic radiation and relaxation parameters are selected in implementation and supported by accepted G6 evidence.",
        "DOCUMENTATION_AMBIGUITY",
        "The implementation is accepted, but the consolidated text still describes this as unresolved.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:387",
            "src/r2d/src/RegionalBoundaryCondition.cpp:309",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:32",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:33",
        ],
        "Documentation ambiguity only; not a mathematical mismatch.",
        "Update model text to cite the accepted characteristic/open-ocean damped policies.",
    ),
    row(
        "R2D-WD-001",
        "Regional2D",
        "Wet/dry treatment",
        "Dry cells use h<h_dry, avoid division by vanishing depth, enforce nonnegative depths, and control momentum.",
        "Hydrostatic reconstruction clips depths and WetDryUpdate/PositivityTimestep enforce admissible updates.",
        "DISCRETE_EQUIVALENT",
        "The required wet/dry behavior is represented by discrete positivity and canonicalisation rules.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:495",
            "src/r2d/src/HydrostaticReconstruction.cpp:81",
            "src/r2d/src/WetDryUpdate.cpp",
            "src/r2d/src/PositivityTimestep.cpp",
        ],
        "No model mismatch.",
        "Maintain shoreline regression tests before changing reconstruction.",
    ),
    row(
        "R2D-RECON-001",
        "Regional2D",
        "Piecewise-constant verification baseline",
        "The numerical formulation should begin with piecewise-constant states for verification.",
        "Current Regional2D residual uses cell-local owner/neighbour states with hydrostatic reconstruction.",
        "DISCRETE_EQUIVALENT",
        "This is the accepted low-order baseline used by the frozen-terrain convergence evidence.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:435",
            "src/r2d/src/WellBalancedResidualEvaluation.cpp",
            "docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A/regional2d_r6_numerical_method_audit.json",
        ],
        "The R6 non-convergence diagnosis is a numerical-method limitation, not a model mismatch.",
        "Treat higher-order reconstruction as a scheme improvement.",
    ),
    row(
        "R2D-RECON-002",
        "Regional2D",
        "Limited linear reconstruction",
        "Limited linear reconstruction may be introduced after constant/linear manufactured-field evidence.",
        "No production MUSCL/limited-gradient Regional2D solver is active in the baseline.",
        "DEFERRED",
        "The model text frames this as a later numerical upgrade, not a required physical model feature.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:437",
            "docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A/regional2d_r6_diagnosis.json",
        ],
        "This is the intended next numerical scheme improvement area.",
        "Proceed only as a numerical-discretisation change with frozen physics and terrain.",
    ),
    row(
        "R2D-TIME-001",
        "Regional2D",
        "Explicit SSP Runge-Kutta",
        "The baseline uses explicit FE for component tests and SSPRK2/SSPRK3 for production.",
        "RegionalTimeIntegration supports FE, SSPRK2, and SSPRK3 with stable timestep checks.",
        "EXACT_MATCH",
        "The documented explicit time-integration family is implemented.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:513",
            "src/r2d/src/RegionalTimeIntegration.cpp:453",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:5",
        ],
        "No model mismatch.",
        "Temporal-convergence work remains separate from spatial scheme changes.",
    ),
    row(
        "R2D-TIME-002",
        "Regional2D",
        "CFL and positivity restrictions",
        "A gravity-wave CFL timestep is used, with additional source/relaxation restrictions.",
        "CflTimestep, PositivityTimestep, RegionalSourceTimestep, and RelaxationTimestep estimates are combined.",
        "DISCRETE_EQUIVALENT",
        "The implementation uses the documented timestep controls in discrete form.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:515",
            "src/r2d/src/RegionalTimeIntegration.cpp:88",
            "src/r2d/src/RegionalTimeIntegration.cpp:156",
            "src/r2d/src/CflTimestep.cpp",
        ],
        "No blocker.",
        "Preserve timestep policy when isolating spatial convergence.",
    ),
    row(
        "R2D-TIME-003",
        "Regional2D",
        "Accept/retry step sequence",
        "The model solve sequence allows accepting a step or reducing dt and retrying.",
        "attempt_regional_explicit_step returns accepted or retry_with_smaller_timestep based on stable estimates.",
        "IMPLEMENTATION_EXTENSION",
        "The retry machinery is an implementation robustness mechanism around the explicit model.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:529",
            "src/r2d/src/RegionalTimeIntegration.cpp:172",
            "src/r2d/src/RegionalTimeIntegration.cpp:523",
        ],
        "No physics-model issue.",
        "Keep retry diagnostics out of physical-parameter invariance checks.",
    ),
    row(
        "R2D-PARAM-001",
        "Regional2D",
        "Gravity",
        "The regional shallow-water pressure and wave speed use gravitational acceleration g.",
        "Case policy sets gravity_m_per_s2=9.80665 for Kamaishi and C1A frozen-terrain baselines.",
        "EXACT_MATCH",
        "The accepted baseline parameter is explicitly configured.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:315",
            "cases/kamaishi_delivery/case_spec.json:95",
            "docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A/physical_parameter_invariance.json",
        ],
        "No parameter mismatch.",
        "Keep gravity frozen for spatial-method experiments.",
    ),
    row(
        "R2D-PARAM-002",
        "Regional2D",
        "Manning coefficient",
        "Manning coefficients are case-data inputs; spatial variation is unresolved.",
        "Accepted Kamaishi and frozen-terrain baselines use uniform n=0.025.",
        "DISCRETE_EQUIVALENT",
        "Uniform roughness is a case-data simplification within the documented input space.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:348",
            "cases/kamaishi_delivery/case_spec.json:106",
            "docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A/physical_parameter_invariance.json",
        ],
        "No model mismatch.",
        "Do not vary roughness in frozen spatial-convergence studies.",
    ),
    row(
        "CPL-EXPORT-001",
        "Coupling",
        "Regional handoff variables",
        "The regional model exports h, eta, qx, qy, and b along the local inlet section.",
        "Coupling exporter writes metadata and samples with depth, momentum_x, momentum_y, bed_elevation, free_surface_elevation.",
        "EXACT_MATCH",
        "The export contract contains the required handoff variables.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:552",
            "src/r2d_case_runner/src/RegionalFileCaseRunner.cpp:1011",
            "src/r2d_case_runner/src/RegionalFileCaseRunner.cpp:1074",
        ],
        "No coupling-contract mismatch.",
        "Keep contract_version increments for future schema changes.",
    ),
    row(
        "CPL-MAP-001",
        "Coupling",
        "Normal and tangential projection",
        "Regional q is projected onto the local inlet normal and tangential directions.",
        "openfoam_replay computes qn and qt from momentum_x/y and configured normal/tangent vectors.",
        "EXACT_MATCH",
        "The projection matches the one-way coupling operator.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:588",
            "tools/openfoam/openfoam_replay.py:751",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:19",
        ],
        "No issue.",
        "Retain projection diagnostics for replay regression tests.",
    ),
    row(
        "CPL-MAP-002",
        "Coupling",
        "Depth-uniform velocity lift",
        "A depth-uniform vertical inlet profile is the simplest shallow-water-consistent baseline.",
        "Replay conversion requires mapping.velocity_profile=depth_uniform and sets vertical_velocity to the configured value, defaulting to zero.",
        "DISCRETE_EQUIVALENT",
        "The implementation uses the documented simplest baseline and rescales to preserve discrete discharge over wet faces.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:607",
            "tools/openfoam/openfoam_replay.py:358",
            "tools/openfoam/openfoam_replay.py:772",
            "tests/fixtures/openfoam/synthetic_replay/replay_config_production.json:57",
        ],
        "No coupling-model mismatch.",
        "Document depth-uniform lift as accepted, not merely unresolved.",
    ),
    row(
        "CPL-MAP-003",
        "Coupling",
        "Free-surface alpha reconstruction",
        "alpha_in is 1 below eta_R and 0 above eta_R, with a resolved transition across local interface cells.",
        "Replay conversion computes per-face alpha fractions from eta, bed elevation, and vertical face bounds.",
        "DISCRETE_EQUIVALENT",
        "The discrete face-fraction mapping is the finite-volume equivalent of the Heaviside alpha rule.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:597",
            "tools/openfoam/openfoam_replay.py:678",
            "tools/openfoam/openfoam_replay.py:793",
        ],
        "No issue.",
        "Keep alpha bounds tests for replay changes.",
    ),
    row(
        "CPL-MAP-004",
        "Coupling",
        "Discharge preservation",
        "The inlet mapping preserves section-integrated normal discharge.",
        "Replay conversion records target and reconstructed normal/tangential discharge residuals.",
        "DISCRETE_EQUIVALENT",
        "The implementation preserves discharge in the discrete support/face quadrature used by the local inlet.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:588",
            "tools/openfoam/openfoam_replay.py:817",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:21",
        ],
        "No issue.",
        "Use residual diagnostics as replay acceptance evidence.",
    ),
    row(
        "CPL-ONEWAY-001",
        "Coupling",
        "One-way replay",
        "Regional-to-local coupling is one-way; local reflections are not fed back to Regional2D.",
        "OpenFOAM replay consumes archived Regional2D boundaryData and never updates Regional2D state.",
        "EXACT_MATCH",
        "The implementation matches the accepted one-way coupling assumption.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:609",
            "tools/openfoam/openfoam_replay.py:1301",
            "docs/workstream/wbs-reconciliation/g6_theoretical_model_gate.md:40",
        ],
        "No one-way-coupling mismatch.",
        "Keep two-way coupling deferred unless reflection evidence requires it.",
    ),
    row(
        "L3D-EQS-001",
        "Local3D",
        "Incompressible two-phase URANS",
        "The local model solves incompressible immiscible two-phase Navier-Stokes/URANS with VOF.",
        "OpenFOAM Foundation 11 incompressibleVoF dictionaries are generated by openfoam_replay.",
        "DISCRETE_EQUIVALENT",
        "The repository configures an adopted solver backend for the documented equations.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:638",
            "docs/workstream/SWE - Software/SWE-L3D/g6_openfoam_authority_record.json",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:22",
        ],
        "No local governing-equation mismatch.",
        "Keep backend/version authority records with future OpenFOAM changes.",
    ),
    row(
        "L3D-VOF-001",
        "Local3D",
        "VOF phase transport",
        "The physical alpha equation is advective and bounded between 0 and 1.",
        "OpenFOAM alpha.water transport is configured and post-run acceptance checks bound alpha.",
        "DISCRETE_EQUIVALENT",
        "The adopted backend implements bounded finite-volume VOF transport.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:661",
            "tools/openfoam/openfoam_replay.py:1564",
            "tools/openfoam/openfoam_replay.py:2507",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:24",
        ],
        "No issue.",
        "Keep alpha boundedness acceptance checks.",
    ),
    row(
        "L3D-VOF-002",
        "Local3D",
        "Interface compression",
        "The finite-volume VOF implementation adds bounded interface compression.",
        "OpenFOAM incompressibleVoF provides the production compressive VOF implementation.",
        "IMPLEMENTATION_EXTENSION",
        "Compression is a documented numerical extension of the physical alpha advection equation.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:669",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:24",
        ],
        "No physical-model mismatch.",
        "Do not interpret compression as an additional physical phase source.",
    ),
    row(
        "L3D-SST-001",
        "Local3D",
        "k-omega SST turbulence",
        "The selected turbulence baseline is Menter k-omega SST URANS.",
        "Generated OpenFOAM momentumTransport sets simulationType RAS and model kOmegaSST.",
        "EXACT_MATCH",
        "The configured turbulence model matches the selected local closure.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:678",
            "tools/openfoam/openfoam_replay.py:1473",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:27",
        ],
        "No issue.",
        "Keep LES comparison deferred.",
    ),
    row(
        "L3D-MAT-001",
        "Local3D",
        "Water-air mixture properties",
        "Mixture properties depend on alpha for water and air density/viscosity.",
        "Generated OpenFOAM phaseProperties and physicalProperties files configure water and air phases.",
        "DISCRETE_EQUIVALENT",
        "The backend receives the required two-phase material-property configuration.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:630",
            "tools/openfoam/openfoam_replay.py:1470",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:25",
        ],
        "No issue.",
        "Record any future material calibration separately.",
    ),
    row(
        "L3D-BC-IN-001",
        "Local3D",
        "Coupling inlet",
        "The local inlet is forced by one-way regional replay.",
        "Generated U and alpha.water inlet patches use timeVaryingMappedFixedValue from boundaryData.",
        "DISCRETE_EQUIVALENT",
        "The local inlet receives the mapped regional time series through OpenFOAM native boundaryData.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:728",
            "tools/openfoam/openfoam_replay.py:1476",
            "docs/workstream/SWE - Software/SWE-L3D/g6_openfoam_authority_record.json:93",
        ],
        "No baseline boundary mismatch.",
        "Keep one-way inlet as immutable when testing Regional2D scheme changes.",
    ),
    row(
        "L3D-BC-OUT-001",
        "Local3D",
        "Outlet boundary",
        "The local outlet is open/radiation with absorption where required.",
        "Production replay uses pressureInletOutletVelocity, prghTotalPressure p0=0, variableHeightFlowRate, turbulence inletOutlet, and outlet isotropicDamping.",
        "DISCRETE_EQUIVALENT",
        "The selected OpenFOAM open-ocean policy implements the intended outlet role.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:729",
            "tools/openfoam/openfoam_replay.py:1479",
            "tools/openfoam/openfoam_replay.py:1491",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:32",
        ],
        "No issue.",
        "Keep reflection coefficients in boundary-policy evidence.",
    ),
    row(
        "L3D-BC-SIDE-001",
        "Local3D",
        "Lateral boundary",
        "Artificial side boundaries represent continuation of the ocean/coastal field and are not rigid walls.",
        "Production replay uses open-ocean field conditions and lateral isotropicDamping zones; legacy symmetry remains only for schema 1.0.0 fixtures.",
        "DISCRETE_EQUIVALENT",
        "The accepted production policy matches the documented physical role.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:730",
            "tools/openfoam/openfoam_replay.py:1494",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:33",
        ],
        "No issue.",
        "Do not regress production laterals to rigid or symmetry-only behavior.",
    ),
    row(
        "L3D-BC-TOP-001",
        "Local3D",
        "Atmosphere boundary",
        "The top boundary is open atmosphere with reference pressure and suitable phase/velocity treatment.",
        "Generated atmosphere patch uses open velocity/pressure/alpha policies.",
        "DISCRETE_EQUIVALENT",
        "The OpenFOAM boundary choices implement the intended open atmospheric role.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:731",
            "tools/openfoam/openfoam_replay.py:1506",
            "tools/openfoam/openfoam_replay.py:1522",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:34",
        ],
        "No issue.",
        "Keep top boundary policy in generated boundary_policy.json.",
    ),
    row(
        "L3D-BC-TERRAIN-001",
        "Local3D",
        "Terrain wall",
        "Fixed terrain/topography is impermeable with wall shear treatment.",
        "Generated terrain wall fields use noSlip, fixedFluxPressure, zeroGradient alpha, and wall functions.",
        "DISCRETE_EQUIVALENT",
        "The OpenFOAM wall policy implements fixed terrain with wall-shear closure.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:732",
            "tools/openfoam/openfoam_replay.py:1485",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:35",
        ],
        "No issue.",
        "Keep wall-function evidence with production Local3D runs.",
    ),
    row(
        "L3D-BC-BARRIER-001",
        "Local3D",
        "Rigid barrier wall",
        "The barrier is a fixed rigid obstacle with pressure and viscous traction extraction.",
        "Generated barrier patch is a wall and enables the OpenFOAM forces function object when present.",
        "DISCRETE_EQUIVALENT",
        "The rigid-wall local model is implemented through OpenFOAM wall patches and forces extraction.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:733",
            "tools/openfoam/openfoam_replay.py:1610",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:36",
        ],
        "No issue.",
        "Keep deformable/porous barriers out of current baseline claims.",
    ),
    row(
        "L3D-FRC-001",
        "Local3D",
        "Force and moment extraction",
        "Hydrodynamic loading is extracted from pressure and viscous tractions on the barrier.",
        "OpenFOAM forces function object is configured over the barrier patch.",
        "DISCRETE_EQUIVALENT",
        "Integrated force/moment extraction is available, while full load convergence remains a verification task.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:733",
            "tools/openfoam/openfoam_replay.py:1612",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:38",
        ],
        "No model mismatch; downstream validation remains open.",
        "Do not require full force convergence before Regional2D spatial-scheme work.",
    ),
    row(
        "L3D-TIME-001",
        "Local3D",
        "Adaptive timestep/CFL control",
        "Local3D uses adaptive timestep/CFL controls for stable VOF evolution.",
        "Generated controlDict and timestep_policy evidence configure maxCo, maxAlphaCo, and maxDeltaT policy around OpenFOAM controls.",
        "DISCRETE_EQUIVALENT",
        "The local solver timestep policy is implemented through backend configuration and repository evidence.",
        [
            "docs/workstream/wbs-reconciliation/g6_theoretical_model_gate.md:65",
            "tools/openfoam/openfoam_replay.py:1559",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:37",
        ],
        "No blocker for Regional2D spatial work.",
        "Keep formal Local3D timestep convergence deferred.",
    ),
    row(
        "HLD-BIDIR-001",
        "Hybrid",
        "Two-way coupling",
        "Two-way coupling is necessary only if local reflection materially changes the incident regional field.",
        "No two-way Regional2D feedback path is implemented.",
        "DEFERRED",
        "The model explicitly accepts one-way replay as the current baseline.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:611",
            "docs/workstream/wbs-reconciliation/g6_theoretical_model_gate.md:40",
        ],
        "No current-baseline mismatch.",
        "Revisit only if reflection/placement evidence invalidates one-way replay.",
    ),
    row(
        "HLD-LES-001",
        "Hybrid",
        "Production LES",
        "LES may be used as a later high-fidelity comparison.",
        "The implemented operational local baseline is URANS k-omega SST.",
        "DEFERRED",
        "LES is outside the G6/C1A accepted baseline.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:678",
            "docs/workstream/wbs-reconciliation/g6_theoretical_model_gate.md:93",
        ],
        "No blocker.",
        "Keep LES in future verification/validation planning.",
    ),
    row(
        "HLD-FSI-001",
        "Hybrid",
        "Fluid-structure interaction and structural damage",
        "FSI, deformable structures, damage, scour, and full impact metrics are outside the current gate.",
        "No FSI/damage/scour solver is implemented.",
        "DEFERRED",
        "These features are excluded from the accepted theoretical-model gate.",
        [
            "docs/workstream/wbs-reconciliation/g6_theoretical_model_gate.md:90",
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex",
        ],
        "No blocker for numerical scheme work.",
        "Keep excluded features out of model-consistency acceptance criteria.",
    ),
    row(
        "HLD-DISP-001",
        "Hybrid",
        "Regional dispersive correction",
        "No dispersive regional baseline is selected.",
        "Regional2D implements NLSWE, not Boussinesq/dispersive propagation.",
        "DEFERRED",
        "Dispersive physics are a future model extension, not a missing required term.",
        [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex",
            "src/r2d/src/ShallowWaterFlux.cpp",
        ],
        "No current-baseline mismatch.",
        "Do not introduce dispersive terms as part of spatial-order repairs.",
    ),
]


ASSUMPTIONS = [
    {
        "id": "ASM-001",
        "assumption": "The accepted model authority is the consolidated model plus G6 traceability/gate evidence where newer than fragmented RES-MOD wording.",
        "basis": "G6 accepted traceability explicitly records passive earthquake transfer, one-way replay, and Local3D policies.",
        "impact": "Older RES-MOD wording can be a documentation ambiguity without blocking numerical scheme work.",
    },
    {
        "id": "ASM-002",
        "assumption": "C1A frozen-terrain spatial studies keep terrain, earthquake source, Manning, Coriolis, boundary policy, and replay contracts fixed.",
        "basis": "The audit is a model-to-code traceability check before numerical scheme changes.",
        "impact": "Observed C1A spatial behavior is attributed to discretisation unless a listed core mismatch appears.",
    },
    {
        "id": "ASM-003",
        "assumption": "OpenFOAM Foundation 11 is treated as an adopted implementation authority for the local incompressibleVoF/SST equations.",
        "basis": "Existing G6 authority records and replay tests exercise generated dictionaries rather than reimplementing URANS/VOF in repository code.",
        "impact": "Traceability cites generated configuration and authority evidence for Local3D operators.",
    },
    {
        "id": "ASM-004",
        "assumption": "The current Regional2D earthquake baseline is instantaneous initialization, not finite-rise-time moving-bed forcing.",
        "basis": "Consolidated model text and G6 traceability select passive free-surface transfer.",
        "impact": "Dynamic rupture is classified as deferred/documentation repair, not a blocking implementation mismatch.",
    },
    {
        "id": "ASM-005",
        "assumption": "Uniform Manning n=0.025 is a case-data decision for the accepted Kamaishi/frozen-terrain baselines.",
        "basis": "Spatially varying roughness classes are noted as unresolved model inputs.",
        "impact": "Roughness variation is not part of the numerical-convergence isolation.",
    },
    {
        "id": "ASM-006",
        "assumption": "The Local3D production lateral/open-ocean policy supersedes the legacy synthetic schema 1.0.0 symmetry fixture.",
        "basis": "G6 traceability records schema 1.1.0 production open_ocean_damped policy and reflection metrics.",
        "impact": "Legacy symmetry tests remain compatibility tests, not the current physical boundary baseline.",
    },
]


DEFERRED_FEATURES = [
    {
        "id": "DEF-001",
        "feature": "Limited linear/MUSCL Regional2D reconstruction",
        "reason": "Numerical scheme improvement target after P0 frozen-physics diagnosis.",
        "current_classification": "DEFERRED",
    },
    {
        "id": "DEF-002",
        "feature": "Dynamic moving-bed earthquake rupture",
        "reason": "Accepted baseline uses instantaneous passive free-surface transfer.",
        "current_classification": "DEFERRED",
    },
    {
        "id": "DEF-003",
        "feature": "Two-way Regional2D-Local3D coupling",
        "reason": "One-way replay is accepted unless reflection materially changes the incident field.",
        "current_classification": "DEFERRED",
    },
    {
        "id": "DEF-004",
        "feature": "Production LES",
        "reason": "URANS k-omega SST is the operational Local3D baseline.",
        "current_classification": "DEFERRED",
    },
    {
        "id": "DEF-005",
        "feature": "FSI, structural damage, deformable barriers, scour",
        "reason": "Excluded from the G6 theoretical-model gate.",
        "current_classification": "DEFERRED",
    },
    {
        "id": "DEF-006",
        "feature": "Observation calibration and validation",
        "reason": "Explicitly outside G6 and C1A numerical convergence scope.",
        "current_classification": "DEFERRED",
    },
    {
        "id": "DEF-007",
        "feature": "Dispersive regional propagation",
        "reason": "Current regional model is NLSWE.",
        "current_classification": "DEFERRED",
    },
    {
        "id": "DEF-008",
        "feature": "Surrogate/ML optimisation and full impact metric suite",
        "reason": "Later workstream outside current model-code consistency gate.",
        "current_classification": "DEFERRED",
    },
]


NUMERICAL_CHOICES = [
    {
        "id": "NUM-001",
        "choice": "Rusanov flux",
        "model_or_numerical": "numerical discretisation",
        "classification": "EXACT_MATCH",
        "decision": "Retained as robust reference flux; alternatives such as HLL/HLLC are controlled future scheme tests.",
    },
    {
        "id": "NUM-002",
        "choice": "Hydrostatic reconstruction",
        "model_or_numerical": "numerical discretisation of bed source",
        "classification": "DISCRETE_EQUIVALENT",
        "decision": "Accepted discrete equivalent of the continuous bed-slope source and well-balancing requirement.",
    },
    {
        "id": "NUM-003",
        "choice": "Piecewise-constant state reconstruction",
        "model_or_numerical": "numerical discretisation",
        "classification": "DISCRETE_EQUIVALENT",
        "decision": "Current P0 baseline; R6 evidence points to numerical-order limits, not missing physics.",
    },
    {
        "id": "NUM-004",
        "choice": "Limited linear/MUSCL reconstruction",
        "model_or_numerical": "numerical discretisation",
        "classification": "DEFERRED",
        "decision": "Legitimate next C1A improvement if physics/terrain remain frozen.",
    },
    {
        "id": "NUM-005",
        "choice": "Manning semi-implicit split update",
        "model_or_numerical": "numerical source integration",
        "classification": "DISCRETE_EQUIVALENT",
        "decision": "Implements the same damping source direction with a stable update.",
    },
    {
        "id": "NUM-006",
        "choice": "VOF interface compression",
        "model_or_numerical": "Local3D numerical discretisation",
        "classification": "IMPLEMENTATION_EXTENSION",
        "decision": "Backend numerical mechanism for bounded interface capturing, not extra physical forcing.",
    },
    {
        "id": "NUM-007",
        "choice": "One-way replay",
        "model_or_numerical": "coupling architecture",
        "classification": "EXACT_MATCH",
        "decision": "Accepted hybrid baseline; two-way feedback is a deferred model extension.",
    },
]


DOC_CONFLICTS = [
    {
        "id": "DOC-CONFLICT-001",
        "topic": "Earthquake source formulation",
        "source_a": "RES-MOD-SRC selected approach says dynamic moving-bed earthquake source b=b0+sum_m Delta b_m R_m(t).",
        "source_b": "Consolidated model and G6 matrix select static/passive finite-fault displacement mapped to initial eta with q=0.",
        "implemented_reality": "RegionalEarthquakeInitialisation computes effective displacement, builds post-event bed/depth, and zeroes post-event momentum at initialization.",
        "classification": "DOCUMENTATION_AMBIGUITY",
        "recommended_resolution": "Revise RES-MOD-SRC to state instantaneous passive initial transfer is the current accepted baseline; mark dynamic moving-bed rupture as deferred.",
        "blocking_for_numerical_scheme_work": False,
        "evidence": [
            "docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/RES-MOD-SRC - Physical Source Terms and Constitutive Closures/res-mod-src.tex:502",
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:364",
            "src/r2d/src/RegionalEarthquakeInitialisation.cpp:550",
        ],
    },
    {
        "id": "DOC-CONFLICT-002",
        "topic": "Radiation/sponge policy selection",
        "source_a": "Consolidated model v1.0 still marks exact radiation condition and sponge profile as unresolved.",
        "source_b": "G6 traceability records accepted characteristic/open-ocean damped Regional2D/Local3D boundary evidence and reflection metrics.",
        "implemented_reality": "Regional2D characteristic radiation and relaxation zones exist; Local3D production replay uses open_ocean_damped with isotropicDamping.",
        "classification": "DOCUMENTATION_AMBIGUITY",
        "recommended_resolution": "Update consolidated/RES-MOD boundary wording to cite accepted G6 boundary policy and leave only parameter tuning as future evidence.",
        "blocking_for_numerical_scheme_work": False,
        "evidence": [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex:387",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:32",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md:33",
        ],
    },
]


CORE_AREAS = {
    "governing_equations": ["R2D-EQS-001", "R2D-EQS-002", "R2D-FLUX-001", "R2D-FLUX-002", "L3D-EQS-001"],
    "physical_source_model": [
        "R2D-SRC-BED-001",
        "R2D-SRC-FRIC-001",
        "R2D-SRC-COR-001",
        "R2D-SRC-VISC-001",
        "R2D-SRC-EQ-001",
        "R2D-SRC-EQ-002",
        "R2D-SRC-EQ-003",
    ],
    "baseline_boundary_roles": [
        "R2D-BC-001",
        "R2D-BC-002",
        "R2D-BC-003",
        "L3D-BC-IN-001",
        "L3D-BC-OUT-001",
        "L3D-BC-SIDE-001",
        "L3D-BC-TOP-001",
        "L3D-BC-TERRAIN-001",
        "L3D-BC-BARRIER-001",
    ],
    "one_way_coupling": ["CPL-EXPORT-001", "CPL-MAP-001", "CPL-MAP-002", "CPL-MAP-003", "CPL-MAP-004", "CPL-ONEWAY-001"],
}


def classification_counts(rows: list[TraceRow]) -> dict[str, int]:
    counts = Counter(row.classification for row in rows)
    return {name: counts.get(name, 0) for name in sorted(CLASSIFICATIONS)}


def core_status(rows: list[TraceRow]) -> dict[str, object]:
    by_id = {row.requirement_id: row for row in rows}
    status = {}
    blockers: list[str] = []
    for area, ids in CORE_AREAS.items():
        area_rows = [by_id[row_id] for row_id in ids]
        area_blockers = [
            item.requirement_id
            for item in area_rows
            if item.classification in {"MISMATCH", "NOT_IMPLEMENTED_REQUIRED"} or item.blocking_for_numerical_scheme_work
        ]
        status[area] = {
            "row_count": len(area_rows),
            "blocking_rows": area_blockers,
            "status": "clear" if not area_blockers else "blocked",
        }
        blockers.extend(area_blockers)
    return {
        "status_by_area": status,
        "blocking_rows": blockers,
        "may_proceed_to_numerical_scheme_improvement": not blockers,
    }


def markdown_table(headers: list[str], rows: list[list[object]]) -> str:
    escaped = []
    for row_values in rows:
        escaped.append([str(value).replace("\n", " ").replace("|", "\\|") for value in row_values])
    lines = ["| " + " | ".join(headers) + " |", "| " + " | ".join("---" for _ in headers) + " |"]
    lines.extend("| " + " | ".join(row_values) + " |" for row_values in escaped)
    return "\n".join(lines)


def write_csv(rows: list[TraceRow]) -> None:
    fields = [
        "requirement_id",
        "scope",
        "component",
        "model_requirement",
        "implementation_route",
        "classification",
        "finding",
        "evidence",
        "impact",
        "recommended_action",
        "blocking_for_numerical_scheme_work",
    ]
    with CSV_PATH.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fields)
        writer.writeheader()
        for trace_row in rows:
            item = asdict(trace_row)
            item["evidence"] = "; ".join(trace_row.evidence)
            writer.writerow(item)


def write_json(rows: list[TraceRow], counts: dict[str, int], status: dict[str, object]) -> None:
    payload = {
        "audit_id": "C1A-MODEL-IMPLEMENTATION-TRACEABILITY-001",
        "title": "Regional2D/Local3D mathematical model to implementation traceability audit",
        "generated_by": "tools/verification/convergence/model_traceability_audit.py",
        "scope": [
            "Regional2D NLSWE governing equations and source terms",
            "Regional2D boundary roles and numerical method choices",
            "Regional-to-Local3D one-way replay coupling",
            "Local3D OpenFOAM URANS-VOF baseline configuration",
        ],
        "explicit_exclusions": [
            "h300 temporal production simulations",
            "Local3D production solver execution",
            "calibration or observation validation",
            "physics or numerical changes",
        ],
        "classification_allowed_values": sorted(CLASSIFICATIONS),
        "model_freeze_classification": "MODEL_CONSISTENT_WITH_DOCUMENTATION_FIXES",
        "model_freeze_rationale": (
            "No MISMATCH or NOT_IMPLEMENTED_REQUIRED rows were found in core governing equations, "
            "physical source model, baseline boundary roles, or one-way coupling. Two documentation "
            "ambiguities require wording updates but do not change the accepted implementation decision."
        ),
        "classification_counts": counts,
        "core_acceptance": status,
        "authority_documents": [
            "docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex",
            "docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/res-mod.tex",
            "docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/RES-MOD-EQS - Governing Equations and State Variables/res-mod-eqs.tex",
            "docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/RES-MOD-SRC - Physical Source Terms and Constitutive Closures/res-mod-src.tex",
            "docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/RES-MOD-IBC - Initial, Boundary and Interface Condition Models/res-mod-ibc.tex",
            "docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/RES-MOD-MUL - Multidimensional and Multifidelity Coupling Strategy/res-mod-mul.tex",
            "docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/RES-MOD-ASM - Assumptions, Applicability and Model Limitations/res-mod-asm.tex",
            "docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/RES-MOD-SPEC - Integrated Mathematical Model Specification/res-mod-spec.tex",
            "docs/workstream/wbs-reconciliation/g6_theoretical_model_gate.md",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md",
            "docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.csv",
        ],
        "traceability_rows": [asdict(item) for item in rows],
        "assumptions": ASSUMPTIONS,
        "deferred_features": DEFERRED_FEATURES,
        "numerical_vs_model_choices": NUMERICAL_CHOICES,
        "documentation_conflicts": DOC_CONFLICTS,
        "proceed_decision": (
            "Proceed to Regional2D numerical scheme improvement under frozen G6 terrain/physics constraints. "
            "Do not alter physical source terms, baseline boundary roles, or coupling semantics as part of that work."
        ),
    }
    JSON_PATH.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def write_markdown(rows: list[TraceRow], counts: dict[str, int], status: dict[str, object]) -> None:
    blocking_rows = status["blocking_rows"]
    lines = [
        "# Regional2D Model Implementation Traceability Audit",
        "",
        "Audit ID: `C1A-MODEL-IMPLEMENTATION-TRACEABILITY-001`",
        "",
        "Model freeze classification: `MODEL_CONSISTENT_WITH_DOCUMENTATION_FIXES`",
        "",
        "Decision: proceed to Regional2D numerical scheme improvement with frozen G6 terrain, physics, boundary roles, and one-way coupling. The audit found no `MISMATCH` or `NOT_IMPLEMENTED_REQUIRED` rows in the core acceptance areas. The remaining issues are documentation wording repairs.",
        "",
        "## Classification Counts",
        "",
        markdown_table(["classification", "count"], [[key, value] for key, value in counts.items()]),
        "",
        "## Core Acceptance",
        "",
        markdown_table(
            ["area", "row_count", "status", "blocking_rows"],
            [
                [area, data["row_count"], data["status"], ", ".join(data["blocking_rows"]) or "none"]
                for area, data in status["status_by_area"].items()
            ],
        ),
        "",
        f"Blocking rows overall: `{', '.join(blocking_rows) if blocking_rows else 'none'}`.",
        "",
        "## Traceability Matrix",
        "",
        markdown_table(
            [
                "id",
                "scope",
                "component",
                "classification",
                "finding",
                "impact",
                "action",
            ],
            [
                [
                    item.requirement_id,
                    item.scope,
                    item.component,
                    item.classification,
                    item.finding,
                    item.impact,
                    item.recommended_action,
                ]
                for item in rows
            ],
        ),
        "",
        "## Assumptions",
        "",
        markdown_table(
            ["id", "assumption", "basis", "impact"],
            [[item["id"], item["assumption"], item["basis"], item["impact"]] for item in ASSUMPTIONS],
        ),
        "",
        "## Deferred Features",
        "",
        markdown_table(
            ["id", "feature", "reason", "classification"],
            [
                [item["id"], item["feature"], item["reason"], item["current_classification"]]
                for item in DEFERRED_FEATURES
            ],
        ),
        "",
        "## Numerical vs Model Choices",
        "",
        markdown_table(
            ["id", "choice", "category", "classification", "decision"],
            [
                [item["id"], item["choice"], item["model_or_numerical"], item["classification"], item["decision"]]
                for item in NUMERICAL_CHOICES
            ],
        ),
        "",
        "## Documentation Conflicts",
        "",
        markdown_table(
            ["id", "topic", "classification", "implemented reality", "recommended resolution", "blocking"],
            [
                [
                    item["id"],
                    item["topic"],
                    item["classification"],
                    item["implemented_reality"],
                    item["recommended_resolution"],
                    item["blocking_for_numerical_scheme_work"],
                ]
                for item in DOC_CONFLICTS
            ],
        ),
        "",
        "## Authority Documents",
        "",
        "- `docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex`",
        "- `docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/*`",
        "- `docs/workstream/wbs-reconciliation/g6_theoretical_model_gate.md`",
        "- `docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.md`",
        "- `docs/workstream/wbs-reconciliation/g6_model_traceability_matrix.csv`",
        "",
        "## Source Areas Inspected",
        "",
        "- Regional2D state, flux, hydrostatic reconstruction, residual, sources, wet/dry, boundaries, relaxation, timestep, case runner, and CSV/coupling export.",
        "- Coupling section export and OpenFOAM replay boundaryData conversion.",
        "- Local3D OpenFOAM replay dictionaries, turbulence/wall/damping/timestep policies, boundary reflection evidence, and synthetic smoke evidence.",
        "",
    ]
    MD_PATH.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    rows = list(TRACE_ROWS)
    ids = [item.requirement_id for item in rows]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate requirement_id in trace rows")
    counts = classification_counts(rows)
    status = core_status(rows)
    write_json(rows, counts, status)
    write_csv(rows)
    write_markdown(rows, counts, status)
    print(f"wrote {JSON_PATH}")
    print(f"wrote {CSV_PATH}")
    print(f"wrote {MD_PATH}")
    print(json.dumps({"rows": len(rows), "counts": counts, "core": status}, indent=2))


if __name__ == "__main__":
    main()
