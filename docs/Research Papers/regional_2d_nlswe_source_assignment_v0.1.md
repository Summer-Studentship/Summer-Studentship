# Regional 2D NLSWE Source Assignment

Date: 2026-07-07

Status: Companion note for `mendeley_paper_tag_register_v0.3.xlsx`

The XLSX paper register was not rewritten. This note records the source roles
needed by the regional 2D NLSWE specification so the spreadsheet can be updated
manually without losing provenance.

| Model component | Assigned source role | Current citation/register status |
| --- | --- | --- |
| Finite-volume control-volume formulation | General FVM face-flux, cell-average and polygonal control-volume basis | Use existing RES-NUM finite-volume evidence; add/confirm a general FVM text in the spreadsheet if required |
| SWE/NLSWE theory | Regional two-dimensional depth-averaged governing model and long-wave assumptions | RES-MOD cites `saito2014dispersion`; RES-PHY/RES-DAT cite Tohoku observation/source papers |
| Hydrostatic reconstruction, well-balancing and positivity | Lake-at-rest preservation, bed-source balance and non-negative depth | `kurganov2007wellbalanced` in `res-num.bib`; add Audusse-style hydrostatic reconstruction source to the register if not already present |
| HLL/HLLC shallow-water fluxes | Primary HLLC flux and HLL/HLLE dry/unsafe fallback | Spreadsheet update required if no HLL/HLLC shallow-water flux source is already tagged |
| Wetting/drying | Dry reset, draining limiter, positivity and shoreline transition | `dutykh2011volna` and `kurganov2007wellbalanced` currently support the finite-volume wet/dry branch; add a dedicated wet/dry source if absent |
| Tohoku regional NLSWE modelling | Event-scale propagation, inundation and validation context | `wei2013japan`, `fujii2011tsunamisource`, `mori2012nationwide`, `fine2013japan`, `grilli2013tohoku` where present in domain bibliographies |
| Dynamic/static tsunami source modelling | Static fallback and dynamic moving-bed source roles | `fujii2011tsunamisource` and `grilli2013tohoku`; final source dataset remains RES-DAT-SRC work |
| Sponge/open-boundary treatment | Radiation/open boundary and sponge damping definitions | Spreadsheet update required if no open-boundary/sponge source is already tagged |
| Bathymetry/topography datasets | Static bed, datum conversion, mesh projection and QGIS dependency | Source role recorded; exact dataset source remains RES-DAT-GEO work |
| Fault-source/subfault models | Subfault geometry, slip, rupture timing and bed increments | `fujii2011tsunamisource` is the first candidate; exact final dataset remains open |

Do not treat this note as a complete bibliography. Its purpose is role
assignment and spreadsheet follow-up for the regional model gate.
