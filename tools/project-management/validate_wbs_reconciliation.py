#!/usr/bin/env python3
"""Validate generated WBS/G6 reconciliation artifacts."""

from __future__ import annotations

import csv
import json
import sys
from pathlib import Path


OUT_DIR = Path("docs/workstream/wbs-reconciliation")
VALID_DISPOSITIONS = {
    "complete_native",
    "complete_adopted_backend",
    "partial",
    "superseded",
    "deferred",
    "not_started",
}
VALID_GATE_RELEVANCE = {"g6_required", "post_g6", "excluded_from_current_scope"}
COMPLETE = {"complete_native", "complete_adopted_backend", "superseded", "deferred"}


def load_json(name: str):
    return json.loads((OUT_DIR / name).read_text(encoding="utf-8"))


def fail(message: str) -> int:
    print(f"validation failed: {message}", file=sys.stderr)
    return 1


def main() -> int:
    inventory = load_json("github_issue_inventory.json")["issues"]
    hierarchy = load_json("wbs_hierarchy.json")
    ledger = load_json("wbs_evidence_ledger.json")
    plan = load_json("wbs_mutation_plan.json")
    gaps = load_json("g6_remaining_gaps.json")
    matrix_path = OUT_DIR / "g6_model_traceability_matrix.csv"

    wbs_ids = [row["wbs_id"] for row in ledger]
    if len(wbs_ids) != len(set(wbs_ids)):
        return fail("WBS IDs are not unique")
    if len(inventory) != len(ledger):
        return fail("inventory and ledger counts differ")

    known = set(wbs_ids)
    for row in ledger:
        parent = row.get("parent_wbs")
        if parent and parent not in known:
            return fail(f"{row['wbs_id']} references missing parent {parent}")
        if row["recommended_disposition"] not in VALID_DISPOSITIONS:
            return fail(f"{row['wbs_id']} has invalid disposition {row['recommended_disposition']}")
        if row["gate_relevance"] not in VALID_GATE_RELEVANCE:
            return fail(f"{row['wbs_id']} has invalid gate relevance {row['gate_relevance']}")
        if row["current_state"] == "closed" and row["recommended_disposition"] in COMPLETE and not row.get("evidence"):
            return fail(f"closed issue {row['wbs_id']} has no evidence")

    nodes = hierarchy["nodes"]
    by_wbs = {row["wbs_id"]: row for row in ledger}
    for wbs_id, node in nodes.items():
        row = by_wbs[wbs_id]
        if (
            row["gate_relevance"] == "g6_required"
            and row["recommended_disposition"] in {"complete_native", "complete_adopted_backend"}
        ):
            for child in node.get("children", []):
                if by_wbs[child]["gate_relevance"] != "g6_required":
                    continue
                child_disp = by_wbs[child]["recommended_disposition"]
                if child_disp not in COMPLETE:
                    return fail(f"completed parent {wbs_id} has incomplete required child {child}")

    plan_by_issue = {row["issue_number"]: row for row in plan}
    if len(plan_by_issue) != len(plan):
        return fail("mutation plan contains duplicate issue numbers")
    for row in plan:
        newly_closing = row["intended_state"] == "closed" and row["current_state"] != "closed"
        if newly_closing:
            if not row.get("comment_body"):
                return fail(f"issue #{row['issue_number']} closes without a comment")
            if not row.get("evidence"):
                return fail(f"issue #{row['issue_number']} closes without evidence")
            if row["wbs_id"] in by_wbs and by_wbs[row["wbs_id"]]["recommended_disposition"] in {"partial", "deferred"}:
                return fail(f"open partial/deferred issue #{row['issue_number']} would be closed")
        if row["wbs_id"].startswith("SWE-GUI") and row["intended_state"] == "closed" and row["current_state"] != "closed":
            return fail(f"open GUI issue #{row['issue_number']} would be closed")

    with matrix_path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if len(rows) < 34:
        return fail("G6 traceability matrix is missing required component rows")
    for row in rows:
        if not row.get("implementation_authority"):
            return fail(f"matrix row {row.get('model_component')} has no implementation authority")
        if not row.get("wbs_id"):
            return fail(f"matrix row {row.get('model_component')} has no WBS ID")

    required_gap_fields = {
        "gap_id",
        "wbs_id",
        "description",
        "why_it_blocks_g6",
        "current_evidence",
        "required_change",
        "required_test",
        "required_acceptance",
        "estimated_scope",
    }
    for gap in gaps:
        missing = required_gap_fields - set(gap)
        if missing:
            return fail(f"gap {gap.get('gap_id')} missing fields {sorted(missing)}")

    print("WBS reconciliation validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
