# Navigation Placeholder Contract v0.1

Navigation order is fixed: `data`, `domain`, `mesh`, `physics`, `solver`, `run`, `post_processing`.

Every page uses the reusable `PlaceholderPage` component and displays `G0 shell placeholder` plus `No scientific workflow is implemented in this page.`

Downstream owners:

| Section | Owners |
| --- | --- |
| Data | SWE-DAT-CFG, SWE-DAT-MAN, SWE-GUI-CAS |
| Domain | SWE-GEO-*, SWE-GUI-CAS |
| Mesh | SWE-GEO-MSH, SWE-FVM-MSH, SWE-GUI-VIS |
| Physics | SWE-R2D-*, SWE-L3D-*, SWE-GUI-CAS |
| Solver | SWE-R2D-*, SWE-L3D-*, SWE-GUI-RUN |
| Run | SWE-ARC-SVC, SWE-GUI-RUN, SWE-GUI-LOG |
| Post-processing | SWE-CPL-MET, SWE-CPL-CMP, SWE-GUI-POST, SWE-GUI-VIS |

Navigation is frontend-only and has no case or run lifecycle side effects.
