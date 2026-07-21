# Local 3D Model Research Staging v0.1

**Project:** Studentship - Generating Tsunami Energy-Dissipating Barriers Using ML  
**Workstream:** Research / Mathematical Modelling / Numerical Methods  
**Component:** Local 3D tsunami-barrier interaction model  
**Status:** Research gates completed; full mathematical model still to be written  
**Intended paper directory:** `Research Papers/3D/`

---

## 1. Purpose of this staging document

This document records the local 3D model research decisions made so far. It is **not yet the final mathematical model**.

The next documentation step is to convert these research decisions into a full implementable mathematical specification, equivalent in quality and completeness to the completed regional 2D NLSWE model specification.

Current status:

$$
\boxed{\text{3D local model research direction established; full mathematical model pending.}}
$$

---

## 2. Relationship to the regional 2D model

The regional model is the large-domain tsunami propagation solver:

$$
\boxed{\text{2D horizontal depth-averaged NLSWE finite-volume model}}
$$

The local 3D model is the high-resolution coastal/barrier interaction solver:

$$
\boxed{\text{3D two-phase incompressible Navier-Stokes + VOF}}
$$

The models are connected by one-way forcing:

$$
\boxed{\text{regional 2D} \rightarrow \text{local 3D}}
$$

The local 3D model does not feed back into the regional model during the studentship baseline.

---

## 3. Completed local-model research gates

| Gate | Topic | Current decision | Status |
|---|---|---|---|
| L3D-1 | Governing fluid identity | 3D incompressible two-phase Navier-Stokes + VOF | Research direction established |
| L3D-2 | Turbulence | URANS k-omega SST baseline; LES deferred | Established for studentship scope |
| L3D-3 | Domain / terrain / geometry abstraction | Fixed local terrain; modular fixed barrier geometry | Established provisionally |
| L3D-4 | 2D-to-3D forcing | One-way regional-to-local forcing | Established provisionally |
| L3D-5 | Boundary conditions | Inlet forcing, open/radiation outlet, lateral open-ocean boundaries, open top, fixed terrain/barrier | Established provisionally |
| L3D-6 | Load extraction | Fixed-geometry hydrodynamic force and moment extraction | Established |
| L3D-7 | Performance metrics | Energy, transmission, run-up, overtopping, force, impulse, moment | Established |
| L3D-8 | Numerical-method requirements | Finite volume, bounded VOF, pressure-correction, CFL constraints | Established provisionally |
| L3D-9 | Validation hierarchy | Solver capability, Tohoku hydrodynamics, impact/loading validation | Established |
| L3D-10 | Scope closure | Local 3D research scope closed; mathematical model pending | Current state |

---

## 4. Baseline assumptions now locked

$$
\boxed{\begin{aligned}
&\text{Fluid model: 3D incompressible two-phase Navier-Stokes.}\\
&\text{Free surface: VOF water volume fraction }\alpha.\\
&\text{Air phase: retained numerically for free-surface capture.}\\
&\text{Turbulence: URANS k-omega SST baseline.}\\
&\text{Coupling: one-way regional 2D to local 3D.}\\
&\text{Geometry: fixed terrain and fixed rigid barrier.}\\
&\text{Barrier classes: wall-type and obstacle-type.}\\
&\text{FSI: deferred; only fluid loads extracted.}\\
&\text{Porosity/damage/deformation: deferred extensions.}
\end{aligned}}
$$

---

## 5. Preliminary equation set already identified

The following equations have been identified as the basis of the future full mathematical model. They must later be expanded, sourced, and organised into a complete mathematical specification.

### 5.1 Continuity

$$
\nabla \cdot \mathbf u = 0
$$

### 5.2 Momentum

$$
\frac{\partial(\rho\mathbf u)}{\partial t}
+
\nabla\cdot(\rho\mathbf u\otimes\mathbf u)
=
-\nabla p
+
\nabla\cdot\left[\mu_{eff}(\nabla\mathbf u+\nabla\mathbf u^T)\right]
+
\rho\mathbf g
$$

with:

$$
\mu_{eff}=\mu+\mu_t
$$

### 5.3 VOF transport

$$
\frac{\partial\alpha}{\partial t}
+
\nabla\cdot(\alpha\mathbf u)=0
$$

### 5.4 Mixture properties

$$
\rho(\alpha)=\alpha\rho_w+(1-\alpha)\rho_a
$$

$$
\mu(\alpha)=\alpha\mu_w+(1-\alpha)\mu_a
$$

### 5.5 Barrier traction and loads

$$
\mathbf t_B=-p\mathbf n+\boldsymbol\tau\mathbf n
$$

$$
\mathbf F_B(t)=\int_{\Gamma_B}\mathbf t_B\,dA
$$

$$
\mathbf M_B(t)=\int_{\Gamma_B}(\mathbf x-\mathbf x_0)\times\mathbf t_B\,dA
$$

These expressions are currently a research scaffold. The full mathematical model must later define every variable, surface, source term, boundary term, closure, and output consistently.

---

## 6. Boundary partition now established

The local fluid boundary is partitioned as:

$$
\partial\Omega_F
=
\Gamma_{in}
\cup
\Gamma_{out}
\cup
\Gamma_{side}
\cup
\Gamma_{top}
\cup
\Gamma_T
\cup
\Gamma_B
$$

| Boundary | Meaning | Treatment |
|---|---|---|
| $\Gamma_{in}$ | offshore/local inlet | one-way 2D-to-3D forcing |
| $\Gamma_{out}$ | downstream/inland outlet | open/radiation + absorption |
| $\Gamma_{side}$ | artificial lateral ocean cuts | lateral open-ocean radiation + relaxation |
| $\Gamma_{top}$ | upper air boundary | open atmosphere |
| $\Gamma_T$ | terrain/topography | fixed no-slip terrain |
| $\Gamma_B$ | rigid barrier/obstacle | fixed no-slip solid boundary |

Important correction:

$$
\boxed{\Gamma_{side}\text{ represents unmodelled lateral ocean, not a physical wall.}}
$$

---

## 7. Geometry abstraction now established

The local fluid domain is represented as:

$$
\Omega_F=\Omega_L\setminus\Omega_B
$$

where $\Omega_B(\boldsymbol\theta)$ is a fixed solid geometry parameterised by geometry variables.

Two geometry families are retained:

1. **Wall-type barriers**  
   Laterally extended structures such as seawalls, raised ridges, or continuous defence walls.

2. **Obstacle-type barriers**  
   Compact or repeated objects, where the first studentship-scale model studies one object under different placements/orientations.

No single final geometry is hard-coded into the mathematical model.

---

## 8. One-way 2D-to-3D forcing scaffold

The regional model supplies:

$$
h_R,\quad \eta_R,\quad q_{x,R},\quad q_{y,R},\quad b_R
$$

Recover depth-averaged velocity:

$$
u_R=\frac{q_{x,R}}{h_R},\qquad v_R=\frac{q_{y,R}}{h_R}
$$

Map to local 3D inlet fields:

$$
\alpha_{in}=H(\eta_R-z)
$$

$$
\mathbf u_{in}=(u_R,v_R,0)^T
$$

$$
p_{in}=\rho_wg\max(\eta_R-z,0)
$$

This is a provisional depth-uniform, hydrostatic inlet reconstruction. The full mathematical model must later state its assumptions and limitations explicitly.

---

## 9. Validation hierarchy now established

Validation is separated into:

$$
\boxed{\text{V1: solver capability} \rightarrow \text{V2: Tohoku hydrodynamics} \rightarrow \text{V3: impact/loading}}
$$

| Level | Purpose | Examples |
|---|---|---|
| V1 | Check solver capability | dam break, solitary wave run-up, wave impact on wall, flow around object |
| V2 | Check Tohoku hydrodynamic realism | run-up, inundation, free-surface height, flow velocity |
| V3 | Check impact/load credibility | pressure, peak force, impulse, moment on fixed structures |

Hydrodynamic validation and impact validation are intentionally separate.

---

## 10. Required source/PDF assignment process

All relevant 3D-model sources should be stored under:

```text
Research Papers/3D/
```

Each paper should then be assigned to one or more evidence roles:

| Evidence role | Needed for |
|---|---|
| 3D RANS / VOF tsunami modelling | justifying local 3D branch |
| 2D-to-3D hybrid/nested coupling | one-way forcing from regional model |
| VOF/free-surface capture | phase-fraction equation and interface treatment |
| k-omega SST / turbulence in tsunami waves | turbulence closure |
| numerical wave-tank absorption/relaxation | inlet/outlet/side boundary treatment |
| tsunami impact force studies | load extraction and impact metrics |
| Tohoku hydrodynamic observations | run-up/inundation validation |
| canonical free-surface benchmarks | solver-capability validation |
| porous/FSI/damage papers | deferred extensions only |

The future full mathematical model should cite sources by **source role**, not only by bibliography entry.

Required citation pattern:

```text
Equation / decision -> modelling role -> supporting source(s)
```

Example:

```text
The VOF transport equation defines the evolution of the water-volume fraction.
Source role: supports Eulerian free-surface capture in a two-phase model.
Supporting papers: [3D-VOF-01], [3D-VOF-02].
```

---

## 11. Reserved section for the full mathematical model

The next major document should be:

```text
local_3d_vof_mathematical_model_spec_v0.1
```

Suggested structure:

1. Purpose and scope
2. Relationship to regional 2D NLSWE model
3. Domain and boundary notation
4. Phase indicator and mixture properties
5. Full governing equations
6. Turbulence closure
7. Geometry and terrain model
8. One-way regional-to-local forcing
9. Local boundary conditions
10. Fixed-barrier load extraction
11. Energy/performance metrics
12. Numerical-method requirements
13. Validation hierarchy
14. Established vs deferred decisions
15. Source/PDF assignment matrix

This document should be the eventual implementation-facing mathematical model.

---

## 12. Established vs pending

### Established at research-decision level

- local 3D model identity;
- VOF free-surface treatment;
- URANS k-omega SST baseline;
- one-way 2D-to-3D forcing;
- fixed terrain and fixed rigid geometry;
- wall-type and obstacle-type geometry classes;
- boundary partition;
- lateral open-ocean side-boundary interpretation;
- force/moment extraction;
- energy/performance metrics;
- validation hierarchy.

### Pending before implementation-level mathematical model

- complete equation-by-equation specification;
- source/PDF assignment from `Research Papers/3D/`;
- exact turbulence-equation form and coefficients to be documented or deliberately abstracted;
- exact relaxation/radiation boundary formulae;
- exact numerical VOF treatment;
- exact validation datasets and acceptance criteria;
- final separation between mathematical model, numerical model, and later software implementation.

---

## 13. Closure statement

$$
\boxed{\text{The local 3D model research gates are complete enough to begin the full mathematical model specification.}}
$$

This staging document should be used as the bridge between the research discussion and the next repo-native mathematical model document.
