# R19 Domain Geometry Register

Status: `COMPLETE`
Generated: `2026-08-12T21:52:46Z`
Branch: `feat/r19-tikz-domain-figures`
HEAD: `2e2f843ab55404e332a9780410f852705fa0ba95`

This register records figure quantities used by the R19 QGIS + TikZ hybrid computational-domain package. `UNRESOLVED` values are intentionally not estimated.

| Symbol | Value | Unit | Definition | Source | Status | Confidence | Figure usage |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `lat_E, lon_E` | {"latitude": 38.297, "longitude": 142.373} | deg | USGS official origin coordinates used as event reference | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/kamaishi-delivery-corridor-evidence.json` | current-case | HIGH | T1, T2, register |
| `E_E, N_E` | {"x": 620060.6382673722, "y": 4239660.228986675} | m | Event reference transformed to EPSG:32654 | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/kamaishi-delivery-corridor-evidence.json` | current-case | HIGH | T1, T2 |
| `lat_K, lon_K` | {"latitude": 39.2757, "longitude": 141.8858} | deg | Kamaishi proxy coordinate used by the delivery corridor evidence | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/kamaishi-delivery-corridor-evidence.json` | current-case | HIGH | T1 |
| `L_c` | 123319.00726067027 | m | Source-side boundary to selected interface/inland end: pre-extent + event-to-interface + inland extent | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/tohoku-kamaishi-centreline.json` | current-case | HIGH | T1, T2, T3 |
| `L_prop` | 108319.00726067027 | m | Event reference to selected wet nearshore interface along centreline | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/tohoku-kamaishi-centreline.json` | current-case | HIGH | T1, T2 |
| `W_c` | 8000.0 | m | Constant corridor width; narrowing disabled | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/kamaishi-delivery-corridor-evidence.json` | current-case | HIGH | T1, T3 |
| `theta_c` | 338.006313127818 | deg | Source-to-interface bearing clockwise from north in the accepted projected corridor basis | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/kamaishi-delivery-corridor-evidence.json` | current-case | HIGH | T1 |
| `theta_c_W` | 21.993686872182025 | deg | Same bearing as degrees west of grid north for compact figure annotation | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/kamaishi-delivery-corridor-evidence.json` | current-case | HIGH | T1 |
| `L_pre` | 15000.0 | m | Centreline length before the event reference | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/tohoku-kamaishi-centreline.json` | current-case | HIGH | T1, T2 |
| `L_inland` | 0.0 | m | Configured inland extent after selected target/interface | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/tohoku-kamaishi-centreline.json` | current-case | HIGH | T1, T2 |
| `S_off` | 10000.0 | m | Regional2D offshore relaxation/sponge width | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/case.json` | current-case | HIGH | T3 |
| `S_side` | 1000.0 | m | Regional2D side relaxation/sponge width | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/case.json` | current-case | HIGH | T3 |
| `I` | {"projected_m": {"x": 579494.6902478148, "y": 4340096.334024712}, "wgs84": {"latitude": 39.206500468041476, "longitude": 141.920714192236}} | m / deg | Selected wet nearshore interface centre | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/kamaishi-delivery-corridor-evidence.json` | current-case | HIGH | T1, T2, T3 |
| `h_I` | 8.0 | m | Water depth at selected wet nearshore interface centre | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/kamaishi-delivery-corridor-evidence.json` | current-case | HIGH | T2, T3 |
| `profile` | {"bed_elevation_max_m": -22.407207012966843, "bed_elevation_min_m": -1077.4007756119922, "distance_offshore_max_km": 123.21947283937752, "distance_offshore_min_km": 0.09953442129290488, "sample_count": 155} | mixed | Bathymetry profile sampled from frozen R10 h400 Regional2D mesh/S1 lineage | `deliverables/figures/r19_tikz/data/bathymetry_profile.csv` | current-case | HIGH | T2 |
| `L_3D` | 772.6171671085285 | m | G6 Local3D simple_rigid_barrier streamwise length | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/local/simple_rigid_barrier/openfoam_case_summary.json` | current-case/framework | HIGH | T2, T3 |
| `W_3D` | 6996.499999999534 | m | G6 Local3D simple_rigid_barrier span | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/local/simple_rigid_barrier/openfoam_case_summary.json` | current-case/framework | HIGH | T3 |
| `H_3D` | 100.35649860871837 | m | G6 Local3D simple_rigid_barrier vertical domain height | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/local/simple_rigid_barrier/openfoam_case_summary.json` | current-case/framework | HIGH | T3 |
| `x_b` | 463.5703002651171 | m | Simple rigid barrier streamwise position | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/local/simple_rigid_barrier/openfoam_case_summary.json` | current-case/framework | HIGH | T3 |
| `t_b` | 38.630858355426426 | m | Simple rigid barrier streamwise thickness | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/local/simple_rigid_barrier/openfoam_case_summary.json` | current-case/framework | HIGH | T3 |
| `H_b` | 19.315429177713213 | m | Simple rigid barrier height | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/local/simple_rigid_barrier/openfoam_case_summary.json` | current-case/framework | HIGH | T3 |
| `shoreline` | UNRESOLVED | n/a | No rigorously authorised shoreline intersection is promoted; selected wet interface is not relabelled as shoreline | `/home/helios/Projects/Summer-Studentship-r19-tikz/deliverables/figures/r16_publication/sources/qgis/derived/regional_coastline_0m.gpkg` | unresolved | BLOCKED_BY_SOURCE_DATA | T2 caveat |
| `rupture polygon` | UNRESOLVED | n/a | Finite-fault displacement model exists, but no authoritative 2D rupture polygon is used for R19 T1 | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/inputs/data/earthquake/tohoku_vertical_displacement.json` | unresolved | BLOCKED_BY_SOURCE_DATA | T1 exclusion |
| `candidate defence region` | framework only | n/a | G6 simple rigid barrier is a representative replay geometry, not a final defence-placement prescription | `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/local/simple_rigid_barrier/openfoam_case_summary.json` | framework | MEDIUM | T3 |

## Preserved Scientific Authority

- Regional numerical authority: `MODEL_CONSISTENT_WITH_DOCUMENTATION_FIXES`, `GLOBAL_FIRST_ORDER_VERIFIED`, `SECOND_ORDER_VERIFIED`.
- Event result authority: R10 h400 `limited_linear`, `BEST_AVAILABLE_NUMERICALLY_UNCERTAIN`.
- Limitations preserved: real 2011 Tohoku event, verified formulation, not spatially qualified, not physically calibrated, not historically validated.
- Hybrid status: implemented/demonstrated through accepted G6 replay; R10 h400 Local3D replay remains `REPLAY_VOF_BEHAVIOUR_UNRESOLVED`.
- Historical observations preserved: 29 observations, 0 DIRECT, 1 PROXY, 28 TARGET_ONLY; NOWPHAS 802G about 12.273 km outside, DART 21418 about 545 km outside.
