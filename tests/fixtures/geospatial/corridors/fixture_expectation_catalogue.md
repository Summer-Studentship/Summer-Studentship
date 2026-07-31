# Corridor Fixture Expectation Catalogue

| fixture or test name | expected success/failure | expected error code | expected rule_id | expected residual | expected polygon vertex count | expected area | expected perimeter |
| --- | --- | --- | --- | --- | --- | --- | --- |
| axis_aligned_constant_width.json | success | n/a | n/a | area/perimeter 0 | 5 | 150000 | 3200 |
| rotated_constant_width.json | success | n/a | n/a | area/perimeter 0 | 5 | 300000 | 3200 |
| linear_narrowing.json | success | n/a | n/a | area/perimeter 0 | 7 | 90000 | 2540.8997975910737 |
| illustrative_kamaishi_corridor.json | success | n/a | n/a | area/perimeter 0 | 5 | 3000000 | 13000 |
| illustrative_sendai_corridor.json | success | n/a | n/a | area/perimeter 0 | 9 | 1050000 | 10422.02458730867 |
| missing_case.json | failure | geo.corridor.request_invalid | geo.corridor.request.references_present | n/a | n/a | n/a | n/a |
| case_manifest_mismatch.json | failure | geo.corridor.case_manifest_mismatch | geo.corridor.request.identities_match | n/a | n/a | n/a | n/a |
| case_revision_mismatch.json | failure | geo.corridor.case_revision_mismatch | geo.corridor.request.identities_match | n/a | n/a | n/a | n/a |
| trajectory_mismatch.json | failure | geo.corridor.trajectory_mismatch | geo.corridor.request.trajectory_matches | n/a | n/a | n/a | n/a |
| missing_epicentre_point_set.json | failure | geo.corridor.point_set_missing | geo.corridor.request.references_present | n/a | n/a | n/a | n/a |
| missing_target_point_set.json | failure | geo.corridor.point_set_missing | geo.corridor.request.references_present | n/a | n/a | n/a | n/a |
| missing_transformation_record.json | failure | geo.corridor.transformation_record_missing | geo.corridor.request.references_present | n/a | n/a | n/a | n/a |
| coordinate_index_out_of_range.json | failure | geo.corridor.coordinate_index_invalid | geo.corridor.reference.coordinate_exists | n/a | n/a | n/a | n/a |
| nonfinite_epicentre.json | failure | geo.corridor.coordinate_nonfinite | geo.corridor.reference.coordinate_exists | n/a | n/a | n/a | n/a |
| nonfinite_target.json | failure | geo.corridor.coordinate_nonfinite | geo.corridor.reference.coordinate_exists | n/a | n/a | n/a | n/a |
| target_crs_mismatch.json | failure | geo.corridor.reference_mismatch | geo.corridor.reference.target_matches | n/a | n/a | n/a | n/a |
| target_epoch_mismatch.json | failure | geo.corridor.reference_mismatch | geo.corridor.reference.target_matches | n/a | n/a | n/a | n/a |
| target_unit_mismatch.json | failure | geo.corridor.reference_mismatch | geo.corridor.reference.target_matches | n/a | n/a | n/a | n/a |
| missing_point_definition.json | failure | geo.corridor.provenance_invalid | geo.corridor.reference.provenance_complete | n/a | n/a | n/a | n/a |
| missing_source_document.json | failure | geo.corridor.provenance_invalid | geo.corridor.reference.provenance_complete | n/a | n/a | n/a | n/a |
| coincident_points.json | failure | geo.corridor.reference_points_coincident | geo.corridor.reference.points_distinct | n/a | n/a | n/a | n/a |
| origin_mismatch.json | failure | geo.corridor.origin_mismatch | geo.corridor.origin.case_matches_epicentre | origin > tolerance | n/a | n/a | n/a |
| bearing_mismatch.json | failure | geo.corridor.bearing_mismatch | geo.corridor.bearing.case_matches_evidence | bearing > tolerance | n/a | n/a | n/a |
| narrowing_equal_width.json | failure | geo.corridor.narrowing_invalid | geo.corridor.narrowing.rule_supported | n/a | n/a | n/a | n/a |
| side_sponge_consumes_width.json | failure | geo.corridor.sponge_invalid | geo.corridor.sponge.side_inside | n/a | n/a | n/a | n/a |
| clockwise_polygon_control.json | failure | geo.corridor.polygon_clockwise | geo.corridor.polygon.counter_clockwise | n/a | n/a | n/a | n/a |
