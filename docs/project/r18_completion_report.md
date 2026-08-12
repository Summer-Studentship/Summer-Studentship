# R18 Completion Report

1. R18 branch: feat/r18-poster-visual-freeze
2. Worktree: /home/helios/Projects/Summer-Studentship-r18-poster
3. Starting HEAD: 73f9a581740dba8d09ee2cb14c8a26ac22bc3eea
4. Final HEAD: see final response after commit
5. Commits: see final response after commit
6. Worktree clean?: checked after final commit
7. Baseline R17 scene confirmed?: {"bytes": 9305523, "exists": true, "path": "deliverables/figures/r17_closure/sources/blender/figure_C_corridor_bathymetry_3d.blend", "sha256": "4652b2e2d6b117ecc331af8a24bbb97fbe92e883575810643fcfc4dacb0c037a"}
8. Sea-plane opacity candidates: [0.03, 0.05, 0.07]
9. Selected sea-plane opacity: 0.05
10. Lighting/material changes: light neutral background, stronger soft AO/key/fill, same terrain data
11. Corridor line changes: muted coral outline, 1.25x R17 bevel, unchanged footprint
12. Orthographic render path: {"bytes": 2618741, "exists": true, "path": "deliverables/figures/r18_poster/previews/figure_C_camera_orthographic.png", "sha256": "fcf80b587023271c557cb0f7ff3c8e7f6028f2fc9e7b4a6e0da9d828b3ad44fa"}
13. Perspective render path: {"bytes": 2953508, "exists": true, "path": "deliverables/figures/r18_poster/previews/figure_C_camera_perspective.png", "sha256": "e4dd62377b02abfe3eb573ea7a9518d61d1188a098dfbb44d77b095c7d2985be"}
14. Perspective focal length: 82 mm
15. Camera comparison path: {"bytes": 2432782, "exists": true, "path": "deliverables/figures/r18_poster/publication/figure_C_camera_comparison.png", "sha256": "d3af491919820fa673a87e2fd255c9957eb709dcb95447daa1f6bd2c50f97d20"}
16. Selected camera: orthographic
17. Camera-selection rationale: The 82 mm perspective adds depth but weakens geographic footprint readability; orthographic preserves the corridor extent and coastline relationship.
18. Final clean C path: {"bytes": 16090856, "exists": true, "path": "deliverables/figures/r18_poster/publication/figure_C_bathymetry_3d_clean.png", "sha256": "22677c8b3dc53af12c3f4f87c46bd563c751886f2377c08bfbebe923932da563"}
19. Final annotated C path: {"bytes": 9905788, "exists": true, "path": "deliverables/figures/r18_poster/publication/figure_C_bathymetry_3d_annotated.png", "sha256": "c343226392dc71d03bb7ab2ef130c7935d2b83937639c520c92a832e4d53cf80"}
20. Final native resolution: [6200, 3600]
21. Final PDF: {"bytes": 499106, "exists": true, "path": "deliverables/figures/r18_poster/publication/figure_C_bathymetry_3d.pdf", "sha256": "bdb8f00a6c7c0feb1ef11c2cbbf42e8ecdfb64003e145aebd9cf07ca99020537"}
22. Final caption: Oblique Blender terrain visualisation of the accepted Kamaishi Regional2D corridor from the R16/R17 ETOPO 2022 EGM2008 terrain lineage. Vertical relief is exaggerated 4x for interpretation; the blue plane marks z=0 and the muted coral outline is the unchanged computational corridor.
23. Poster classification: PRIMARY
24. Main source map: R16 Figure B source raster and corridor layers
25. Geographic inset source: R16 Figure A context raster and event/Kamaishi layers
26. Bathymetry inset source: R16 S1/R10 h400 centreline sampling
27. Corridor styling: {"fill_alpha": 0.2, "geometry": "unchanged corridor_polygon.geojson", "stroke": "#d66e4b"}
28. Colourbar range: [-1500.0, 1300.0]
29. Elevation datum: EGM2008 height, positive up
30. Final PDF: {"bytes": 1781882, "exists": true, "path": "deliverables/figures/r18_poster/publication/figure_A1_tohoku_kamaishi_corridor_bathymetry.pdf", "sha256": "15109d42fa5adec5f5691b79d9238b6df97accc139084d2b89264e1d1ca3557e"}
31. Final SVG: {"bytes": 3084604, "exists": true, "path": "deliverables/figures/r18_poster/publication/figure_A1_tohoku_kamaishi_corridor_bathymetry.svg", "sha256": "c7ce8bd564ceb27e9200ef9cd0726f13d9f9c4921b438d99a6dd42e8d37c03ce"}
32. Final PNG: {"bytes": 2804152, "exists": true, "path": "deliverables/figures/r18_poster/publication/figure_A1_tohoku_kamaishi_corridor_bathymetry.png", "sha256": "5de35ffa9111a1feecb658b80241609a4a945e3d4f84b209db217cf26b212fbf"}
33. Poster classification: HERO
34. Final title: Simulated free-surface evolution toward Kamaishi
35. Distance-axis convention: Distance offshore from nearshore interface, km; zero is the selected wet nearshore interface.
36. Colour limits: {"center_m": 0.0, "cmap": "RdBu_r", "symmetric_limit_m": 1.25, "type": "diverging"}
37. Contour levels: {"negative_dashed_m": [-1.0, -0.5, -0.25], "positive_solid_m": [0.25]}
38. Display interpolation: none; pcolormesh cell rendering of stored h400 samples
39. Final PDF: {"bytes": 75975, "exists": true, "path": "deliverables/figures/r18_poster/publication/figure_D1_eta_space_time_publication.pdf", "sha256": "eebc46481ec8e10d0168583df30a6b94649d04e941d0892255f7372fcbb1f6fb"}
40. Final SVG: {"bytes": 83181, "exists": true, "path": "deliverables/figures/r18_poster/publication/figure_D1_eta_space_time_publication.svg", "sha256": "3dcc50c043c7f0a0ac08aefd399007c372d23239e1952e8c393291004f4248aa"}
41. Final PNG: {"bytes": 259862, "exists": true, "path": "deliverables/figures/r18_poster/publication/figure_D1_eta_space_time_publication.png", "sha256": "3eca691dbc62eeb47b9011c79115e13a469288cf176bbecdf6f3291e03d4e919"}
42. Poster classification: PRIMARY
43. Final title: Historical observations relative to the Kamaishi corridor
44. NOWPHAS representation: priority offshore target plotted with true nearest-distance segment to corridor
45. 12.3 km distance method: Euclidean nearest point from NOWPHAS 802G to unchanged corridor polygon boundary in EPSG:32654
46. DART inset: validation overview raster with DART 21418 and corridor context
47. Observation subset shown: ["Kamaishi proxy/place", "NOAA_NCEI_TIDE_19236 KAMAISHI TARGET_ONLY", "NOAA_NCEI_SURVEY_24106 PROXY", "PARI_NOWPHAS_802G_KAMAISHI_OFFSHORE TARGET_ONLY", "NOAA_NCEI_DART_21418 TARGET_ONLY inset"]
48. R15 classification preserved?: {"DIRECT": 0, "PROXY": 1, "TARGET_ONLY": 28}
49. Final PDF: {"bytes": 1316398, "exists": true, "path": "deliverables/figures/r18_poster/publication/figure_F_validation_geometry_publication.pdf", "sha256": "847629c068cdc400b5dd8ee6b519abe8bd4574de720dae2ee6192dfd4ed35e20"}
50. Final SVG: {"bytes": 2371786, "exists": true, "path": "deliverables/figures/r18_poster/publication/figure_F_validation_geometry_publication.svg", "sha256": "78ac7b4b7c4e6781a2bb397f3ea6b21d9e6fb608008ec7b483fe5ad8215e602c"}
51. Final PNG: {"bytes": 2207777, "exists": true, "path": "deliverables/figures/r18_poster/publication/figure_F_validation_geometry_publication.png", "sha256": "a1a8bb11538c5c50c1c38f62cca98f27ff5a7fda59e2664fe1e75133249be9bd"}
52. Poster classification: SECONDARY
53. D2 final classification: REPORT_ONLY
54. E final classification: DROP_FROM_POSTER
55. S1 final classification: REPORT_ONLY
56. Final contact-sheet path: {"bytes": 2503939, "exists": true, "path": "deliverables/figures/r18_poster/publication/r18_poster_contact_sheet.png", "sha256": "922930b63a4dcdbfbce2f0ee9903cd3e8924aaa9c4e324dcd69f27af4dbdf25d"}
57. HERO figure: A1
58. Primary figures: ["C", "D1"]
59. Secondary figures: ["F"]
60. Report-only figures: ["D2", "S1"]
61. Drop-from-poster figures: ["E"]
62. Strongest overall visual: A1
63. Strongest geographic visual: A1
64. Strongest scientific-result visual: D1
65. Strongest validation visual: F
66. Handoff path: {"bytes": 3961, "exists": true, "path": "docs/project/r18_poster_visual_handoff.md", "sha256": "9395eaab201fcd5e56a7e0431147323929c256c8495ee6a46053668bbf380c49"}
67. Captions complete?: True
68. Allowed claims complete?: True
69. Caveats complete?: True
70. Suggested physical sizes included?: True
71. Recommended Page-2 figure: A1
72. Recommended companion figure: D1
73. Reasoning: A1 combines context/corridor/profile; D1 carries the strongest frozen numerical-result evidence.
74. Blender checks: rendered previews/final; final PNG dimensions checked
75. QGIS checks: {"broken_layers": [], "checked_at_utc": "2026-08-12T07:56:12Z", "layouts": ["01_TOHOKU_EVENT_CORRIDOR", "02_CORRIDOR_BATHYMETRY", "03_HYBRID_DOMAIN", "04_VALIDATION_GEOMETRY", "05_CORRIDOR_BATHYMETRY_OBLIQUE"], "project": {"bytes": 51536, "exists": true, "path": "deliverables/figures/r16_publication/sources/qgis/tohoku_kamaishi_publication.qgz", "sha256": "4cdb7da0b9764267e99398537773cc0c7fbf10224047b477184bd69136f93bf1"}, "qgis_version": "QGIS 4.2.0-Bel\u00e9m do Par\u00e1 'Bel\u00e9m do Par\u00e1' (exported)", "schema": {"name": "tsunami.r18.qgis_project_check", "version": "1.0.0"}, "status": "PASS"}
76. Matplotlib checks: A1/D1/F generated with Agg and saved in PDF/SVG/PNG
77. JSON validation: {"deliverables/figures/r18_poster/provenance/figure_A1_tohoku_kamaishi_corridor_bathymetry.provenance.json": "PASS", "deliverables/figures/r18_poster/provenance/figure_C_bathymetry_3d.provenance.json": "PASS", "deliverables/figures/r18_poster/provenance/figure_C_bathymetry_3d_clean_render.json": "PASS", "deliverables/figures/r18_poster/provenance/figure_C_camera_orthographic_render.json": "PASS", "deliverables/figures/r18_poster/provenance/figure_C_camera_perspective_render.json": "PASS", "deliverables/figures/r18_poster/provenance/figure_C_seaplane_alpha_003_render.json": "PASS", "deliverables/figures/r18_poster/provenance/figure_C_seaplane_alpha_005_render.json": "PASS", "deliverables/figures/r18_poster/provenance/figure_C_seaplane_alpha_007_render.json": "PASS", "deliverables/figures/r18_poster/provenance/figure_D1_eta_space_time_publication.provenance.json": "PASS", "deliverables/figures/r18_poster/provenance/figure_F_validation_geometry_publication.provenance.json": "PASS", "deliverables/figures/r18_poster/provenance/qgis_project_validation_status.json": "PASS", "deliverables/figures/r18_poster/provenance/r18_completion_state.json": "PASS", "deliverables/figures/r18_poster/provenance/r18_poster_visual_manifest.json": "PASS"}
78. SVG validation: {"deliverables/figures/r18_poster/publication/figure_A1_tohoku_kamaishi_corridor_bathymetry.svg": "PASS", "deliverables/figures/r18_poster/publication/figure_D1_eta_space_time_publication.svg": "PASS", "deliverables/figures/r18_poster/publication/figure_F_validation_geometry_publication.svg": "PASS"}
79. PDF validation: {"deliverables/figures/r18_poster/publication/figure_A1_tohoku_kamaishi_corridor_bathymetry.pdf": {"bytes": 1781882, "status": "PASS"}, "deliverables/figures/r18_poster/publication/figure_C_bathymetry_3d.pdf": {"bytes": 499106, "status": "PASS"}, "deliverables/figures/r18_poster/publication/figure_D1_eta_space_time_publication.pdf": {"bytes": 75975, "status": "PASS"}, "deliverables/figures/r18_poster/publication/figure_F_validation_geometry_publication.pdf": {"bytes": 1316398, "status": "PASS"}}
80. PNG dimensions: {"deliverables/figures/r18_poster/publication/figure_A1_tohoku_kamaishi_corridor_bathymetry.png": {"height": 4743, "status": "PASS", "width": 4242}, "deliverables/figures/r18_poster/publication/figure_C_bathymetry_3d_annotated.png": {"height": 3600, "status": "PASS", "width": 6200}, "deliverables/figures/r18_poster/publication/figure_C_bathymetry_3d_clean.png": {"height": 3600, "status": "PASS", "width": 6200}, "deliverables/figures/r18_poster/publication/figure_C_camera_comparison.png": {"height": 1160, "status": "PASS", "width": 4000}, "deliverables/figures/r18_poster/publication/figure_C_seaplane_comparison.png": {"height": 1040, "status": "PASS", "width": 4800}, "deliverables/figures/r18_poster/publication/figure_D1_eta_space_time_publication.png": {"height": 2405, "status": "PASS", "width": 3894}, "deliverables/figures/r18_poster/publication/figure_F_validation_geometry_publication.png": {"height": 2718, "status": "PASS", "width": 4203}, "deliverables/figures/r18_poster/publication/r18_poster_contact_sheet.png": {"height": 3000, "status": "PASS", "width": 3800}}
81. Manifest link checks: {"deliverables/figures/r18_poster/publication/figure_A1_tohoku_kamaishi_corridor_bathymetry.pdf": "PASS", "deliverables/figures/r18_poster/publication/figure_A1_tohoku_kamaishi_corridor_bathymetry.png": "PASS", "deliverables/figures/r18_poster/publication/figure_A1_tohoku_kamaishi_corridor_bathymetry.svg": "PASS", "deliverables/figures/r18_poster/publication/figure_C_bathymetry_3d.pdf": "PASS", "deliverables/figures/r18_poster/publication/figure_C_bathymetry_3d_annotated.png": "PASS", "deliverables/figures/r18_poster/publication/figure_C_bathymetry_3d_clean.png": "PASS", "deliverables/figures/r18_poster/publication/figure_C_camera_comparison.png": "PASS", "deliverables/figures/r18_poster/publication/figure_C_seaplane_comparison.png": "PASS", "deliverables/figures/r18_poster/publication/figure_D1_eta_space_time_publication.pdf": "PASS", "deliverables/figures/r18_poster/publication/figure_D1_eta_space_time_publication.png": "PASS", "deliverables/figures/r18_poster/publication/figure_D1_eta_space_time_publication.svg": "PASS", "deliverables/figures/r18_poster/publication/figure_F_validation_geometry_publication.pdf": "PASS", "deliverables/figures/r18_poster/publication/figure_F_validation_geometry_publication.png": "PASS", "deliverables/figures/r18_poster/publication/figure_F_validation_geometry_publication.svg": "PASS", "deliverables/figures/r18_poster/publication/r18_poster_contact_sheet.png": "PASS"}
82. git diff --check: PASS
83. Worktree clean?: see final response after commit
84. No new Regional simulation: True
85. No h250: True
86. No temporal convergence: True
87. No calibration: True
88. No Local3D replay: True
89. No OpenFOAM retuning: True
90. No HDF5 work: True
91. No FSI/ML implementation: True
92. No poster editing: True
93. No report editing: True
94. No WBS/Lucid work: True
95. No custom hybrid schematic work: True
96. No research files deleted/moved/renamed: True
97. No push/merge/rebase/amend: True
98. Protected/unrelated files untouched: True
99. Is Figure C now frozen for the poster?: True
100. Is the combined A1 map frozen?: True
101. Is D1 frozen?: True
102. Is F frozen?: True
103. Are additional quantitative/GIS figures unnecessary?: True
104. Is the scientific poster figure package ready for layout?: True
105. What manual design work remains outside Codex?: Place the frozen figures into the poster layout and adjust surrounding typography/spacing only.
