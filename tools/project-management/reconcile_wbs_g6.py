#!/usr/bin/env python3
"""Generate WBS/G6 reconciliation artifacts.

The script is intentionally administrative: it reads the imported WBS manifest,
fetches public issue state from GitHub when available, and writes audit artifacts
without changing the numerical model.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from collections import defaultdict
from pathlib import Path
from typing import Any


REPO = "Summer-Studentship/Summer-Studentship"
API = f"https://api.github.com/repos/{REPO}"
OUT_DIR = Path("docs/workstream/wbs-reconciliation")
DECISION_PATH = Path("docs/workstream/RES - Research/decisions/openfoam_local3d_backend_adoption.md")
MANIFEST_PATH = Path("tools/project-management/github-wbs-import/github_software_issue_manifest_v0.1.csv")
ISSUE_MAP_PATH = Path("tools/project-management/github-wbs-import/output/github_software_issue_map.json")

VALID_DISPOSITIONS = {
    "complete_native",
    "complete_adopted_backend",
    "partial",
    "superseded",
    "deferred",
    "not_started",
}
VALID_GATE_RELEVANCE = {"g6_required", "post_g6", "excluded_from_current_scope"}

ADOPTED_BACKEND_CLOSE = {
    56: "SWE-L3D-VOF",
    57: "SWE-L3D-MOM",
    58: "SWE-L3D-PRS",
    59: "SWE-L3D-SST",
    63: "SWE-L3D-SOL",
}
LOCAL_PARTIAL = {
    60: "SWE-L3D-WLF",
    61: "SWE-L3D-BC",
    62: "SWE-L3D-TIM",
    64: "SWE-L3D-FRC",
}
COUPLING_CLOSE = {
    65: "SWE-CPL-IFC",
    66: "SWE-CPL-RPL",
    67: "SWE-CPL-MAP",
    68: "SWE-CPL-BC",
}
COUPLING_PARTIAL = {
    69: "SWE-CPL-MET",
    70: "SWE-CPL-CMP",
}
PARENT_STATUS = {8: "SWE-L3D", 9: "SWE-CPL"}


EVIDENCE = {
    "regional2d": [
        "PR #255 / squash commit 6eda108 closes Regional2D domain #7 and children #47-#55.",
        "PR #268 / merge commit c0f10dc adds the file-driven Regional2D case runner and CSV outputs.",
        "src/r2d/src/* implements NLSWE state, flux, well balancing, wet/dry, source terms, boundaries and solve loop.",
        "src/r2d_case_runner/src/RegionalFileCaseRunner.cpp validates file-driven real-case execution and final physical state.",
        "tests/r2d/* and tests/r2d_io/* cover state, flux, wet/dry, boundary, solve-loop and CSV output behavior.",
    ],
    "earthquake": [
        "PR #269 / merge commit 5de4a2f binds USGS Tohoku finite-fault input into Regional2D.",
        "tools/earthquake/tohoku_usgs_finite_fault.py parses USGS basic_inversion.param and writes vertical seabed displacement artifacts.",
        "data/source/earthquake/README.md records the locked USGS source URL and manual acquisition path.",
        "tests/cases/test_tohoku_producer_crs.py covers projected-grid source handling.",
    ],
    "coupling": [
        "PR #269 / merge commit 5de4a2f exports Regional2D coupling metadata.json, samples.csv and history.csv.",
        "PR #270 / merge commit dc416f8 converts G3 coupling exports to OpenFOAM boundaryData and replay diagnostics.",
        "src/coupling/include/tsunami/coupling/SectionExport.hpp records the section export contract.",
        "tools/openfoam/openfoam_replay.py validates sample ordering, support widths, interpolation, dry handling and discharge residuals.",
        "tests/openfoam/test_openfoam_replay.py covers parser rejection, deterministic boundaryData, support widths and discharge residuals.",
    ],
    "openfoam": [
        "PR #270 / merge commit dc416f8 adds OpenFOAM 11 replay conversion, case generation, wrapper and synthetic smoke acceptance.",
        "PR #271 / merge commit 1ba6475 adds the Kamaishi end-to-end delivery pipeline and complete 300 s Local3D replay acceptance.",
        "tools/openfoam/openfoam_replay.py generates OpenFOAM dictionaries for incompressibleVoF, kOmegaSST, fields, boundaries, probes and forces.",
        "tools/openfoam/run_openfoam11.sh runs docker.io/openfoam/openfoam11-paraview510:11 with networking disabled.",
        "tools/cases/kamaishi_delivery.py orchestrates no_defence and simple_rigid_barrier OpenFOAM stages and validates outputs.",
        "tests/openfoam/test_openfoam_replay.py and tests/cases/test_kamaishi_delivery.py cover replay conversion, case generation and delivery controls.",
    ],
    "kamaishi": [
        "PR #271 / merge commit 1ba6475 accepts a real-data pipeline from earthquake source through Regional2D, coupling export, OpenFOAM replay and both Local3D variants.",
        "cases/kamaishi_delivery/case_spec.json records USGS Tohoku, ETOPO 2022, EPSG:32654, EGM2008, Kamaishi interface and model controls.",
        "Accepted run command in PR #271 uses tools/cases/run_kamaishi_hybrid_delivery.py with the etopo-1000m offline profile.",
        "PR #271 records blockMesh/checkMesh/setFields/foamRun/foamToVTK exit status 0 for no_defence and simple_rigid_barrier.",
        "PR #271 records finite final U and p_rgh fields, bounded alpha.water, probe coverage, force coverage and VTK final-time output.",
    ],
    "project": [
        "docs/project-management/manual-wbs-workflow.md states that Project fields are maintained manually.",
        "docs/project-management/github-project-setup.md records the repository's Project field conventions.",
        "The classic repository Projects REST endpoint returned 404; Projects V2 mutation requires GitHub Project OAuth scope that was not available in this session.",
    ],
}


LOCAL_REMAINING = {
    60: [
        "y+ evidence",
        "wall-function applicability",
        "roughness policy",
        "mesh-resolution consistency",
        "load sensitivity",
    ],
    61: [
        "production lateral open-ocean treatment",
        "reflection assessment",
        "boundary-condition benchmark evidence",
        "clear distinction between symmetry test mode and open-ocean mode",
    ],
    62: [
        "diffusive timestep constraint evidence",
        "step-rejection/recovery disposition",
        "formal timestep-convergence evidence",
    ],
    64: [
        "surface pressure distribution",
        "wall shear distribution",
        "traction-vector output",
        "impulse integration",
        "load convergence",
        "approved validation comparison",
    ],
}

COUPLING_REMAINING = {
    69: [
        "run-up",
        "overtopping",
        "transmission coefficient",
        "impulse",
        "energy metrics",
        "approved validation context",
    ],
    70: [
        "wall-type versus obstacle/dissipating barrier comparison",
        "second barrier-class geometry",
        "identical-forcing comparison",
        "comparison metrics",
    ],
}


def request_json(url: str) -> Any:
    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "codex-wbs-g6-reconciliation",
    }
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def read_manifest() -> list[dict[str, str]]:
    with MANIFEST_PATH.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_issue_map() -> dict[str, dict[str, Any]]:
    return json.loads(ISSUE_MAP_PATH.read_text(encoding="utf-8"))


def parse_metadata(body: str | None) -> dict[str, str | None]:
    text = body or ""

    def find(label: str) -> str | None:
        pattern = rf"- \*\*{re.escape(label)}:\*\*\s*(?:`([^`]+)`|(.+))"
        match = re.search(pattern, text)
        if not match:
            return None
        value = match.group(1) or match.group(2) or ""
        return value.strip()

    parent = find("Parent WBS")
    if parent and parent.lower() == "none":
        parent = None
    return {
        "wbs_id": find("WBS ID"),
        "level": find("Level"),
        "parent_wbs": parent,
        "gate": find("Gate"),
        "scope_class": find("Scope class"),
        "initial_status": find("Initial status"),
        "blocked_by": find("Blocked by"),
        "research_inputs": find("Research inputs"),
    }


def fetch_all_issues() -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    for page in range(1, 10):
        batch = request_json(f"{API}/issues?state=all&per_page=100&page={page}")
        if not batch:
            break
        issues.extend(batch)
    return issues


def rate_remaining() -> int | None:
    try:
        payload = request_json("https://api.github.com/rate_limit")
    except Exception:
        return None
    return payload.get("rate", {}).get("remaining")


def fetch_comments(issue_number: int) -> tuple[list[dict[str, Any]], str]:
    try:
        comments = request_json(f"{API}/issues/{issue_number}/comments?per_page=100")
    except urllib.error.HTTPError as exc:
        return [], f"error: HTTP {exc.code}"
    except Exception as exc:  # pragma: no cover - defensive audit output
        return [], f"error: {exc}"
    compact = [
        {
            "id": item.get("id"),
            "url": item.get("html_url"),
            "user": (item.get("user") or {}).get("login"),
            "created_at": item.get("created_at"),
            "updated_at": item.get("updated_at"),
            "body": item.get("body"),
        }
        for item in comments
    ]
    return compact, "fetched"


def fetch_inventory(max_comment_fetches: int) -> tuple[list[dict[str, Any]], dict[int, dict[str, Any]]]:
    manifest = read_manifest()
    issue_map = read_issue_map()
    issue_number_to_wbs = {int(value["number"]): key for key, value in issue_map.items()}
    manifest_by_wbs = {row["WBS ID"]: row for row in manifest}
    raw_issues = fetch_all_issues()
    pr_numbers = {item["number"] for item in raw_issues if "pull_request" in item}
    by_number = {int(item["number"]): item for item in raw_issues if "pull_request" not in item}

    inventory: list[dict[str, Any]] = []
    comment_candidates: list[int] = []
    for wbs_id, mapping in sorted(issue_map.items(), key=lambda item: int(item[1]["number"])):
        number = int(mapping["number"])
        issue = by_number.get(number, {})
        metadata = parse_metadata(issue.get("body")) if issue else {}
        manifest_row = manifest_by_wbs.get(wbs_id, {})
        if not metadata.get("wbs_id"):
            metadata = {
                "wbs_id": wbs_id,
                "level": manifest_row.get("Level"),
                "parent_wbs": manifest_row.get("Parent WBS ID") or None,
                "gate": manifest_row.get("Gate"),
                "scope_class": manifest_row.get("Scope Class"),
                "initial_status": manifest_row.get("Initial Status"),
                "blocked_by": manifest_row.get("Dependencies") or None,
                "research_inputs": manifest_row.get("Research Inputs") or None,
            }
        comments_count = int(issue.get("comments", 0) or 0)
        if comments_count:
            comment_candidates.append(number)
        entry = {
            "wbs_id": wbs_id,
            "issue_number": number,
            "issue_node_id": issue.get("node_id") or mapping.get("node_id"),
            "html_url": issue.get("html_url") or mapping.get("html_url"),
            "title": issue.get("title") or manifest_row.get("Issue Title"),
            "state": issue.get("state"),
            "state_reason": issue.get("state_reason"),
            "milestone": (issue.get("milestone") or {}).get("title"),
            "milestone_number": (issue.get("milestone") or {}).get("number"),
            "type": (issue.get("type") or {}).get("name") if isinstance(issue.get("type"), dict) else None,
            "labels": [label.get("name") for label in issue.get("labels", [])],
            "assignees": [assignee.get("login") for assignee in issue.get("assignees", [])],
            "created_at": issue.get("created_at"),
            "updated_at": issue.get("updated_at"),
            "closed_at": issue.get("closed_at"),
            "comments_count": comments_count,
            "comments": [],
            "comments_fetch_status": "none",
            "body": issue.get("body"),
            "metadata": metadata,
            "manifest": manifest_row,
            "parent_issue_url": issue.get("parent_issue_url"),
            "sub_issues_summary": issue.get("sub_issues_summary"),
            "issue_dependencies_summary": issue.get("issue_dependencies_summary"),
            "linked_prs": [],
        }
        inventory.append(entry)

    # Fetch comments where practical. Prioritize issues that already contain
    # closure evidence or will be referenced by this audit.
    priority = [7, 47, 48, 51, 52, 53, 54, 55, 71, 166, 222]
    ordered = [n for n in priority if n in comment_candidates]
    ordered.extend(n for n in comment_candidates if n not in set(ordered))
    remaining = rate_remaining()
    budget = max_comment_fetches
    if remaining is not None:
        budget = min(budget, max(0, remaining - 5))
    fetched = 0
    by_inv_number = {entry["issue_number"]: entry for entry in inventory}
    for number in ordered:
        entry = by_inv_number[number]
        if fetched >= budget:
            entry["comments_fetch_status"] = "not_fetched_rate_limited"
            continue
        comments, status = fetch_comments(number)
        entry["comments"] = comments
        entry["comments_fetch_status"] = status
        fetched += 1

    for entry in inventory:
        linked = set()
        text_parts = [entry.get("body") or ""]
        text_parts.extend(comment.get("body") or "" for comment in entry.get("comments", []))
        for text in text_parts:
            for match in re.findall(r"#(\d+)", text):
                number = int(match)
                if number in pr_numbers:
                    linked.add(number)
        entry["linked_prs"] = sorted(linked)

    return inventory, {int(item["number"]): item for item in raw_issues if "pull_request" in item}


def route_for_wbs(wbs_id: str, issue_number: int, state: str | None) -> str:
    if issue_number in ADOPTED_BACKEND_CLOSE or wbs_id.startswith("SWE-L3D"):
        return "OpenFOAM adopted"
    if wbs_id.startswith("SWE-CPL"):
        return "Native"
    if wbs_id.startswith("SWE-R2D"):
        return "Native"
    if wbs_id.startswith("SWE-GEO"):
        return "Native + external library"
    if wbs_id.startswith("SWE-DAT-SCH") or wbs_id.startswith("SWE-DAT-XDMF"):
        return "Deferred"
    if wbs_id.startswith("SWE-HPC") or wbs_id.startswith("SWE-STR") or wbs_id.startswith("SWE-GUI"):
        return "Deferred" if state != "closed" else "Native"
    if wbs_id.startswith("SWE-DAT"):
        return "Native + external library"
    return "Native" if state == "closed" else "Deferred"


def disposition_for(entry: dict[str, Any]) -> str:
    wbs_id = entry["wbs_id"]
    number = entry["issue_number"]
    state = entry.get("state")
    if number in ADOPTED_BACKEND_CLOSE:
        return "complete_adopted_backend"
    if number in LOCAL_PARTIAL or number in COUPLING_PARTIAL or number in PARENT_STATUS:
        return "partial"
    if number in COUPLING_CLOSE:
        return "complete_native"
    if state == "closed":
        return "complete_native"
    if wbs_id.startswith("SWE-GUI") or wbs_id.startswith("SWE-HPC") or wbs_id.startswith("SWE-STR"):
        return "deferred"
    if wbs_id.startswith("SWE-DAT-SCH") or wbs_id.startswith("SWE-DAT-XDMF"):
        return "not_started"
    if wbs_id.startswith("SWE-VER-CONV") or wbs_id.startswith("SWE-VER-VAL"):
        return "not_started"
    if wbs_id in {"SWE-L3D", "SWE-CPL"}:
        return "partial"
    if wbs_id.startswith("SWE-L3D") or wbs_id.startswith("SWE-CPL"):
        return "partial"
    return "partial"


def gate_relevance_for(wbs_id: str, issue_number: int) -> str:
    if wbs_id.startswith("SWE-GUI"):
        return "excluded_from_current_scope"
    if wbs_id.startswith("SWE-HPC") or wbs_id.startswith("SWE-STR"):
        return "post_g6"
    if wbs_id.startswith("SWE-DAT-SCH") or wbs_id.startswith("SWE-DAT-XDMF") or wbs_id.startswith("SWE-DAT-CHK"):
        return "post_g6"
    if wbs_id.startswith("SWE-VER-CONV") or wbs_id.startswith("SWE-VER-VAL"):
        return "post_g6"
    if issue_number in {69, 70}:
        return "post_g6"
    if wbs_id.startswith(("SWE-R2D", "SWE-L3D", "SWE-CPL", "SWE-GEO")):
        return "g6_required"
    if wbs_id.startswith(("SWE-DAT-CFG", "SWE-DAT-MAN", "SWE-DAT-ING")):
        return "g6_required"
    if wbs_id.startswith(("SWE-VER-ACC", "SWE-VER-REG", "SWE-VER-UNIT")):
        return "g6_required"
    return "excluded_from_current_scope"


def evidence_for(entry: dict[str, Any], disposition: str) -> list[str]:
    wbs_id = entry["wbs_id"]
    number = entry["issue_number"]
    evidence: list[str] = []
    if wbs_id.startswith("SWE-R2D"):
        evidence.extend(EVIDENCE["regional2d"])
    if number in ADOPTED_BACKEND_CLOSE or number in LOCAL_PARTIAL or wbs_id == "SWE-L3D":
        evidence.extend(EVIDENCE["openfoam"])
        evidence.extend(EVIDENCE["kamaishi"])
    if number in COUPLING_CLOSE or number in COUPLING_PARTIAL or wbs_id == "SWE-CPL":
        evidence.extend(EVIDENCE["coupling"])
        evidence.extend(EVIDENCE["kamaishi"])
    if wbs_id.startswith("SWE-GEO"):
        evidence.append("PR #271 / merge commit 1ba6475 constructs the Kamaishi corridor, CRS target, terrain conditioning and nearshore interface in cases/kamaishi_delivery/case_spec.json and tools/cases/kamaishi_delivery.py.")
    if wbs_id.startswith("SWE-DAT-ING"):
        evidence.extend(EVIDENCE["earthquake"])
    if entry.get("state") == "closed" and not evidence:
        evidence.append("Issue is already closed with state_reason=completed; existing closure evidence is retained in GitHub comments or linked PRs where available.")
    if disposition in {"partial", "not_started", "deferred"} and not evidence:
        evidence.append("No complete acceptance evidence identified in this G6 audit; issue remains open or outside the current gate.")
    return evidence


def remaining_for(entry: dict[str, Any], disposition: str) -> list[str]:
    number = entry["issue_number"]
    wbs_id = entry["wbs_id"]
    if number in LOCAL_REMAINING:
        return LOCAL_REMAINING[number]
    if number in COUPLING_REMAINING:
        return COUPLING_REMAINING[number]
    if number == 8:
        return [
            "production boundary, wall-function and timestep evidence at baseline G6 level",
            "full Local3D wall/load validation remains post-G6",
        ]
    if number == 9:
        return [
            "full impact metrics",
            "barrier-class comparison beyond simplified rigid wall",
        ]
    if wbs_id.startswith("SWE-GUI"):
        return ["deferred until after theoretical-model gate and calibration readiness"]
    if wbs_id.startswith("SWE-HPC"):
        return ["MPI scaling, CPU affinity and GPU feasibility after G6"]
    if wbs_id.startswith("SWE-DAT-SCH"):
        return ["versioned HDF5 schema, adapters and compatibility checks"]
    if wbs_id.startswith("SWE-DAT-XDMF"):
        return ["HDF5-backed XDMF descriptors and validation"]
    if wbs_id.startswith("SWE-VER-CONV"):
        return ["mesh- and timestep-convergence harness and accepted studies"]
    if wbs_id.startswith("SWE-VER-VAL"):
        return ["observation validation and approved comparison harness"]
    if disposition in {"complete_native", "complete_adopted_backend"}:
        return []
    return ["acceptance evidence remains to be produced in the owning workstream"]


def recommended_action(entry: dict[str, Any], disposition: str) -> str:
    number = entry["issue_number"]
    if number in ADOPTED_BACKEND_CLOSE:
        return "Add adopted-backend closure comment and close as completed."
    if number in COUPLING_CLOSE:
        return "Add native coupling evidence closure comment and close as completed."
    if number in LOCAL_PARTIAL or number in COUPLING_PARTIAL or number in PARENT_STATUS:
        return "Add evidence comment and keep open."
    if entry["wbs_id"].startswith("SWE-GUI"):
        return "Keep open if open; mark deferred/excluded in reconciliation ledger and Project fields where supported."
    if entry["wbs_id"].startswith("SWE-HPC"):
        return "Keep open; mark post-G6 in reconciliation ledger and Project fields where supported."
    return "No automatic issue mutation; preserve current state."


def evidence_state(disposition: str) -> str:
    if disposition in {"complete_native", "complete_adopted_backend"}:
        return "Acceptance passed"
    if disposition == "partial":
        return "Implementation linked"
    return "No evidence"


def project_fields(entry: dict[str, Any], disposition: str, gate_relevance: str, route: str) -> dict[str, str]:
    disposition_map = {
        "complete_native": "Complete",
        "complete_adopted_backend": "Complete",
        "partial": "Partial",
        "superseded": "Superseded",
        "deferred": "Deferred",
        "not_started": "Not started",
    }
    gate_map = {
        "g6_required": "G6 required",
        "post_g6": "Post-G6",
        "excluded_from_current_scope": "Excluded",
    }
    if route == "OpenFOAM adopted":
        implementation_route = "OpenFOAM adopted"
    elif route == "Native + external library":
        implementation_route = "External library"
    elif route == "Deferred":
        implementation_route = "Deferred"
    else:
        implementation_route = "Native"
    return {
        "Implementation Route": implementation_route,
        "Evidence State": evidence_state(disposition),
        "WBS Disposition": disposition_map[disposition],
        "Gate Relevance": gate_map[gate_relevance],
        "Current Gate": "G6 — Theoretical Hybrid Model Complete" if gate_relevance == "g6_required" else "",
    }


def build_ledger(inventory: list[dict[str, Any]]) -> list[dict[str, Any]]:
    ledger = []
    for entry in inventory:
        disposition = disposition_for(entry)
        gate_relevance = gate_relevance_for(entry["wbs_id"], entry["issue_number"])
        route = route_for_wbs(entry["wbs_id"], entry["issue_number"], entry.get("state"))
        evidence = evidence_for(entry, disposition)
        remaining = remaining_for(entry, disposition)
        ledger.append(
            {
                "wbs_id": entry["wbs_id"],
                "issue_number": entry["issue_number"],
                "title": entry["title"],
                "level": entry["metadata"].get("level"),
                "parent_wbs": entry["metadata"].get("parent_wbs"),
                "current_state": entry.get("state"),
                "state_reason": entry.get("state_reason"),
                "milestone": entry.get("milestone"),
                "recommended_disposition": disposition,
                "gate_relevance": gate_relevance,
                "implementation_route": route,
                "evidence": evidence,
                "remaining_acceptance": remaining,
                "recommended_issue_action": recommended_action(entry, disposition),
                "project_field_changes": project_fields(entry, disposition, gate_relevance, route),
            }
        )
    return ledger


def build_hierarchy(inventory: list[dict[str, Any]]) -> dict[str, Any]:
    nodes = {}
    children = defaultdict(list)
    for entry in inventory:
        wbs_id = entry["wbs_id"]
        parent = entry["metadata"].get("parent_wbs")
        nodes[wbs_id] = {
            "wbs_id": wbs_id,
            "issue_number": entry["issue_number"],
            "title": entry["title"],
            "level": entry["metadata"].get("level"),
            "parent_wbs": parent,
            "github_parent_issue_url": entry.get("parent_issue_url"),
            "state": entry.get("state"),
            "state_reason": entry.get("state_reason"),
        }
        if parent:
            children[parent].append(wbs_id)
    for wbs_id, node in nodes.items():
        node["children"] = sorted(children.get(wbs_id, []))

    def depth(wbs_id: str, seen: set[str] | None = None) -> int:
        seen = set() if seen is None else seen
        if wbs_id in seen:
            return 0
        seen.add(wbs_id)
        if not nodes[wbs_id]["children"]:
            return 1
        return 1 + max(depth(child, seen) for child in nodes[wbs_id]["children"])

    roots = sorted(wbs_id for wbs_id, node in nodes.items() if not node["parent_wbs"])
    return {
        "generated_at_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "source": {
            "manifest": str(MANIFEST_PATH),
            "issue_map": str(ISSUE_MAP_PATH),
            "repository": REPO,
        },
        "root_wbs_ids": roots,
        "hierarchy_levels": max(depth(root) for root in roots) if roots else 0,
        "nodes": nodes,
    }


def bullet(items: list[str]) -> str:
    return "\n".join(f"- {item}" for item in items)


def closure_comment_adopted(number: int) -> str:
    wbs = ADOPTED_BACKEND_CLOSE[number]
    return f"""Disposition:
    completed through adopted OpenFOAM Foundation 11 backend

Implementation evidence:
    - PR #270 / merge commit dc416f8: OpenFOAM 11 replay conversion, case generation, wrapper and synthetic smoke acceptance.
    - PR #271 / merge commit 1ba6475: complete Kamaishi real-data replay for no_defence and simple_rigid_barrier.

Repository-owned evidence:
    - tools/openfoam/openfoam_replay.py
    - tools/openfoam/regional_to_openfoam_boundary_data.py
    - tools/openfoam/generate_replay_case.py
    - tools/openfoam/run_openfoam11.sh
    - tools/cases/kamaishi_delivery.py
    - tests/openfoam/test_openfoam_replay.py
    - tests/cases/test_kamaishi_delivery.py
    - docs/workstream/SWE - Software/SWE-R2D/openfoam_replay_pipeline_v0.1.md

Acceptance evidence:
    - PR #270 synthetic smoke: blockMesh, checkMesh, setFields, foamRun -solver incompressibleVoF and foamToVTK completed for both variants.
    - PR #271 real-data run: OpenFOAM stages exited 0 for both variants; final U and p_rgh were finite; alpha.water remained within tolerance; probe, force and VTK outputs reached the requested 300 s replay.

Backend boundary:
    - OpenFOAM Foundation 11 implements the numerical {wbs} solver internals, including VOF transport, momentum discretisation, pressure-velocity correction, k-omega SST transport, wall-function evaluation and transient solver sequencing.
    - This repository implements coupling reconstruction, case generation, model selection, boundary-condition dictionaries, execution orchestration, runtime acceptance validation, result extraction and provenance recording."""


def closure_comment_coupling(number: int) -> str:
    title = COUPLING_CLOSE[number]
    details = {
        65: "Coordinate frames, Regional2D inward normal/tangent conventions, vertical datum, section_id and OpenFOAM patch identity are recorded in cases/kamaishi_delivery/case_spec.json, src/coupling/include/tsunami/coupling/SectionExport.hpp and tests/fixtures/openfoam/synthetic_replay/replay_config.json.",
        66: "The G3 metadata/samples/history contract, replay configuration, source paths, source hashes and conversion metadata are produced by RegionalFileCaseRunner.cpp, tools/cases/kamaishi_delivery.py and tools/openfoam/openfoam_replay.py.",
        67: "Sample ordering, support-width inference, piecewise-linear spatial interpolation, time handling and mapping diagnostics are implemented and tested in tools/openfoam/openfoam_replay.py and tests/openfoam/test_openfoam_replay.py.",
        68: "Depth-uniform normal/tangential velocity reconstruction, vertical alpha reconstruction, dry-state treatment and discrete discharge preservation are implemented and tested in tools/openfoam/openfoam_replay.py and tests/openfoam/test_openfoam_replay.py.",
    }[number]
    return f"""Disposition:
    completed as repository-native coupling implementation

Implementation evidence:
    - PR #269 / merge commit 5de4a2f: Regional2D coupling section export.
    - PR #270 / merge commit dc416f8: G3 export to OpenFOAM boundaryData conversion and replay diagnostics.
    - PR #271 / merge commit 1ba6475: accepted Kamaishi real-data delivery run over the selected replay window.

Repository-owned evidence:
    - src/coupling/include/tsunami/coupling/SectionExport.hpp
    - src/r2d_case_runner/src/RegionalFileCaseRunner.cpp
    - tools/openfoam/openfoam_replay.py
    - tools/cases/kamaishi_delivery.py
    - tests/openfoam/test_openfoam_replay.py
    - tests/cases/test_kamaishi_delivery.py

Acceptance evidence:
    - {details}
    - The accepted delivery run records stable source hashes, boundaryData coverage, full replay-window traversal and no_defence/simple_rigid_barrier output validation.

Backend boundary:
    - {title} is repository-owned. OpenFOAM consumes the generated boundaryData and dictionaries but does not define the Regional2D export contract or replay mapping policy."""


def partial_comment(number: int) -> str:
    if number == 60:
        completed = [
            "OpenFOAM case generation selects kOmegaSST RAS turbulence and writes wall-function boundary dictionaries for k, omega and nut.",
            "Terrain and barrier wall patches use noSlip/fixedFluxPressure/zeroGradient-style wall treatment in generated OpenFOAM fields.",
            "Synthetic and Kamaishi real-data OpenFOAM replays reached runtime acceptance with finite fields.",
        ]
        remaining = LOCAL_REMAINING[number]
    elif number == 61:
        completed = [
            "The generated baseline includes timeVaryingMappedFixedValue inlet U/alpha.water, outlet inletOutlet/pressureInletOutletVelocity, atmospheric top and terrain/barrier wall conditions.",
            "The current lateral treatment is symmetryPlane and is suitable as a controlled smoke/test mode, not yet a production open-ocean policy.",
            "BoundaryData coverage is validated against requested end time and the major replay peak.",
        ]
        remaining = LOCAL_REMAINING[number]
    elif number == 62:
        completed = [
            "The generated controlDict enables adjustTimeStep and configures maxCo, maxAlphaCo and maxDeltaT.",
            "Kamaishi delivery derives maxDeltaT from mapped speed, gravity wave speed, local cell dimensions and safety factors.",
            "Runtime acceptance records observed Courant and interface Courant maxima and rejects runs that do not reach the requested replay duration.",
            "OpenFOAM owns accepted internal timestep adaptation; the repository does not currently expose a repo-controlled rejected-step recovery hook.",
        ]
        remaining = LOCAL_REMAINING[number]
    elif number == 64:
        completed = [
            "The simple_rigid_barrier generated case configures the OpenFOAM forces function object on the barrier patch.",
            "PR #271 records non-empty force output through 300 s and finite maximum force/moment magnitudes.",
            "Probe distinction metrics demonstrate the no_defence and wall cases are distinguishable over the complete replay.",
        ]
        remaining = LOCAL_REMAINING[number]
    elif number == 69:
        completed = [
            "OpenFOAM probe output includes p_rgh, U and alpha.water histories for both variants.",
            "The rigid-wall case emits force and moment histories through the complete replay.",
            "Pressure, force and moment are completed subsets only; the full impact-metric deliverable remains open.",
        ]
        remaining = COUPLING_REMAINING[number]
    elif number == 70:
        completed = [
            "The delivery pipeline runs no_defence and simplified full-width rigid-wall variants under identical selected replay forcing.",
            "Probe distinction and force metrics are produced for the wall variant.",
            "This is not the full wall-type versus obstacle/dissipating barrier comparison promised by the deliverable title.",
        ]
        remaining = COUPLING_REMAINING[number]
    elif number == 8:
        completed = [
            "OpenFOAM-adopted solver core is complete for VOF, momentum, pressure correction, k-omega SST and transient solve sequence via #56, #57, #58, #59 and #63.",
            "The repository owns model selection, dictionaries, execution orchestration, acceptance validation, extraction and provenance.",
        ]
        remaining = remaining_for({"issue_number": 8, "wbs_id": "SWE-L3D"}, "partial")
    elif number == 9:
        completed = [
            "Regional/local interface, replay contract, mapping and 3D inlet reconstruction are complete via #65, #66, #67 and #68.",
            "The accepted Kamaishi delivery run exercises coupling export, boundaryData conversion and both Local3D replay variants.",
        ]
        remaining = remaining_for({"issue_number": 9, "wbs_id": "SWE-CPL"}, "partial")
    else:
        completed = ["Baseline implementation evidence is recorded in the G6 reconciliation ledger."]
        remaining = ["Outstanding acceptance is recorded in the G6 reconciliation ledger."]
    return f"""Completed baseline
{bullet(completed)}

Remaining acceptance
{bullet(remaining)}"""


def mutation_plan(inventory: list[dict[str, Any]], ledger: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_issue = {entry["issue_number"]: entry for entry in ledger}
    plan = []
    for entry in inventory:
        number = entry["issue_number"]
        ledger_entry = by_issue[number]
        intended_state = entry.get("state") or "open"
        state_reason = entry.get("state_reason")
        comment = None
        reason = "preserve current state"
        if number in ADOPTED_BACKEND_CLOSE:
            intended_state = "closed"
            state_reason = "completed"
            comment = closure_comment_adopted(number)
            reason = "completed through adopted OpenFOAM Foundation 11 backend"
        elif number in COUPLING_CLOSE:
            intended_state = "closed"
            state_reason = "completed"
            comment = closure_comment_coupling(number)
            reason = "completed as repository-native coupling implementation"
        elif number in LOCAL_PARTIAL or number in COUPLING_PARTIAL or number in PARENT_STATUS:
            intended_state = "open"
            state_reason = None
            comment = partial_comment(number)
            reason = "partial evidence recorded; issue remains open"
        plan.append(
            {
                "issue_number": number,
                "wbs_id": entry["wbs_id"],
                "current_state": entry.get("state"),
                "intended_state": intended_state,
                "intended_state_reason": state_reason,
                "comment_body": comment,
                "project_field_changes": ledger_entry["project_field_changes"],
                "evidence": ledger_entry["evidence"],
                "validation_notes": [
                    "no issue is closed without evidence" if intended_state != "closed" or ledger_entry["evidence"] else "missing evidence",
                    "partial/deferred issues remain open" if ledger_entry["recommended_disposition"] not in {"partial", "deferred"} or intended_state == "open" else "invalid closure",
                ],
                "intended_state_explanation": reason,
            }
        )
    return plan


def traceability_rows() -> list[dict[str, str]]:
    def row(component: str, role: str, equation: str, route: str, authority: str, repo_loc: str, config: str, test: str, run: str, wbs: str, status: str, gap: str = "") -> dict[str, str]:
        return {
            "model_component": component,
            "mathematical_role": role,
            "governing_equation_or_condition": equation,
            "implementation_route": route,
            "implementation_authority": authority,
            "repository_location": repo_loc,
            "configuration_location": config,
            "test_evidence": test,
            "run_evidence": run,
            "wbs_id": wbs,
            "g6_status": status,
            "remaining_gap": gap,
        }

    r2d_test = "tests/r2d/*; tests/r2d_io/*; Regional2D issues #47-#55 closed by PR #255"
    run = "PR #271 accepted Kamaishi 1800 s Regional2D run and 300 s Local3D replay"
    of_test = "tests/openfoam/test_openfoam_replay.py; tests/cases/test_kamaishi_delivery.py"
    of_run = "PR #270 synthetic smoke; PR #271 real-data no_defence and simple_rigid_barrier acceptance"
    return [
        row("NLSWE continuity", "Regional mass conservation", "dh/dt + div(q)=0", "repository-native implementation", "tsunami::r2d", "src/r2d/src/RegionalResidualEvaluation.cpp; src/r2d/src/RegionalSolveLoop.cpp", "cases/kamaishi_delivery/case_spec.json", r2d_test, run, "SWE-R2D-STA", "accepted"),
        row("NLSWE x momentum", "Regional x momentum", "dqx/dt + div(qx u) + g h d eta/dx + sources = 0", "repository-native implementation", "tsunami::r2d", "src/r2d/src/ShallowWaterFlux.cpp; src/r2d/src/RegionalResidualEvaluation.cpp", "cases/kamaishi_delivery/case_spec.json", r2d_test, run, "SWE-R2D-FLX", "accepted"),
        row("NLSWE y momentum", "Regional y momentum", "dqy/dt + div(qy u) + g h d eta/dy + sources = 0", "repository-native implementation", "tsunami::r2d", "src/r2d/src/ShallowWaterFlux.cpp; src/r2d/src/RegionalResidualEvaluation.cpp", "cases/kamaishi_delivery/case_spec.json", r2d_test, run, "SWE-R2D-FLX", "accepted"),
        row("bathymetry source", "Bed elevation and hydrostatic source", "eta=h+zb; hydrostatic reconstruction and bed pressure correction", "repository-native implementation", "tsunami::r2d", "src/r2d/src/RegionalBathymetry.cpp; src/r2d/src/HydrostaticReconstruction.cpp", "cases/kamaishi_delivery/case_spec.json", "tests/r2d/well_balanced_wet_dry_tests.cpp", run, "SWE-R2D-WB", "accepted"),
        row("Manning friction", "Bottom friction source", "Strang-split Manning source term", "repository-native implementation", "tsunami::r2d", "src/r2d/src/RegionalSourceTerms.cpp; src/r2d/src/RegionalSourceTimestep.cpp", "cases/kamaishi_delivery/case_spec.json", "tests/r2d/regional_source_terms_tests.cpp where present; PR #255 closure evidence", run, "SWE-R2D-SRC", "accepted"),
        row("wet/dry handling", "Shoreline positivity", "Hydrostatic reconstruction plus draining-time positivity restriction", "repository-native implementation", "tsunami::r2d", "src/r2d/src/WetDryUpdate.cpp; src/r2d/src/PositivityTimestep.cpp", "cases/kamaishi_delivery/case_spec.json", "tests/r2d/well_balanced_wet_dry_tests.cpp", run, "SWE-R2D-WD", "accepted"),
        row("Regional2D boundaries", "Open/radiation and sponge boundaries", "Characteristic radiation and relaxation-zone source residual", "repository-native implementation", "tsunami::r2d", "src/r2d/src/RegionalBoundaryCondition.cpp; src/r2d/src/RegionalRelaxationZone.cpp", "cases/kamaishi_delivery/case_spec.json", "tests/r2d/regional_boundary_condition_tests.cpp where present; PR #255/#268 evidence", run, "SWE-R2D-BC", "accepted"),
        row("earthquake displacement", "Moving seabed source", "Vertical seabed displacement from USGS finite-fault artifact", "repository-native implementation plus external scientific library", "tools/earthquake + GeoClaw preprocessing", "tools/earthquake/tohoku_usgs_finite_fault.py; src/r2d/src/RegionalEarthquakeInitialisation.cpp", "cases/kamaishi_delivery/case_spec.json; data/source/earthquake/README.md", "tests/cases/test_tohoku_producer_crs.py", run, "SWE-R2D-EQK", "accepted"),
        row("free-surface source transfer", "Passive free-surface initialisation", "eta displacement follows effective seabed displacement in accepted baseline", "repository-native implementation", "tsunami::r2d", "src/r2d/src/RegionalEarthquakeInitialisation.cpp", "cases/kamaishi_delivery/case_spec.json", "tests/r2d/regional_earthquake_initialisation_tests.cpp where present; PR #269 evidence", run, "SWE-R2D-EQK", "accepted"),
        row("corridor geometry", "Kamaishi source-to-interface corridor", "Projected CRS corridor from epicentre to nearshore target", "repository-native implementation plus external geospatial library", "tsunami::geo + PROJ/GDAL adapters", "tools/cases/kamaishi_delivery.py; src/geo/src/CorridorConstruction.cpp", "cases/kamaishi_delivery/case_spec.json", "tests/geo/corridor_construction_tests.cpp; tests/cases/test_kamaishi_delivery.py", run, "SWE-GEO-COR", "accepted"),
        row("terrain interpolation", "Bathymetry/topography conditioning", "Resampling/conditioning to computational terrain grid", "repository-native implementation plus external geospatial library", "tsunami::geo + GDAL", "src/geo/src/TerrainConditioning.cpp; tools/cases/kamaishi_delivery.py", "cases/kamaishi_delivery/case_spec.json", "tests/geo/terrain_conditioning_tests.cpp; tests/cases/test_kamaishi_delivery.py", run, "SWE-GEO-TER", "accepted"),
        row("coupling coordinate transform", "Regional/local frame mapping", "Regional inward normal/tangent to local inlet/span/vertical axes", "repository-native implementation", "tools/openfoam", "tools/openfoam/openfoam_replay.py", "tests/fixtures/openfoam/synthetic_replay/replay_config.json", of_test, of_run, "SWE-CPL-IFC", "accepted"),
        row("coupling spatial interpolation", "Map section samples across inlet", "piecewise_linear_along_section with support-width inference", "repository-native implementation", "tools/openfoam", "tools/openfoam/openfoam_replay.py", "tests/fixtures/openfoam/synthetic_replay/replay_config.json", of_test, of_run, "SWE-CPL-MAP", "accepted"),
        row("coupling temporal handling", "Replay time contract", "Versioned history/samples time series and selected shifted replay window", "repository-native implementation", "tools/cases + tools/openfoam", "tools/cases/select_kamaishi_replay_window.py; tools/cases/kamaishi_delivery.py", "replay_config.json generated by tools/cases/kamaishi_delivery.py", "tests/cases/test_kamaishi_delivery.py", of_run, "SWE-CPL-RPL", "accepted"),
        row("velocity reconstruction", "3D inlet velocity from depth-averaged momentum", "depth_uniform normal/tangential projection; vertical velocity 0", "repository-native implementation", "tools/openfoam", "tools/openfoam/openfoam_replay.py", "tests/fixtures/openfoam/synthetic_replay/replay_config.json", of_test, of_run, "SWE-CPL-BC", "accepted"),
        row("alpha reconstruction", "VOF inlet free surface", "alpha.water face fraction from bed, eta and vertical face bounds", "repository-native implementation feeding adopted backend", "tools/openfoam -> OpenFOAM", "tools/openfoam/openfoam_replay.py", "tests/fixtures/openfoam/synthetic_replay/replay_config.json", of_test, of_run, "SWE-CPL-BC", "accepted"),
        row("discharge preservation", "Discrete inflow consistency", "normal/tangential discharge residual diagnostics", "repository-native implementation", "tools/openfoam", "tools/openfoam/openfoam_replay.py", "replay_diagnostics.csv generated by conversion", of_test, of_run, "SWE-CPL-BC", "accepted"),
        row("3D continuity", "Incompressible two-phase mass conservation", "div(U)=0 pressure-corrected incompressible flow", "adopted OpenFOAM component", "OpenFOAM Foundation 11 incompressibleVoF", "tools/openfoam/openfoam_replay.py generates fvSolution/controlDict", "system/fvSolution; system/controlDict", of_test, of_run, "SWE-L3D-PRS", "accepted_baseline"),
        row("3D momentum", "Local URANS momentum", "Transient incompressible momentum predictor/corrector", "adopted OpenFOAM component", "OpenFOAM Foundation 11 incompressibleVoF", "tools/openfoam/openfoam_replay.py", "system/fvSchemes; system/fvSolution; 0/U", of_test, of_run, "SWE-L3D-MOM", "accepted_baseline"),
        row("VOF transport", "Free-surface transport", "MPLIC/compressive alpha.water transport", "adopted OpenFOAM component", "OpenFOAM Foundation 11 incompressibleVoF", "tools/openfoam/openfoam_replay.py", "system/fvSchemes; system/fvSolution; 0/alpha.water", of_test, of_run, "SWE-L3D-VOF", "accepted_baseline"),
        row("mixture properties", "Water/air material properties", "Two immiscible phases with density, viscosity and surface tension", "configuration of adopted backend", "repository dictionaries consumed by OpenFOAM", "tools/openfoam/openfoam_replay.py", "constant/phaseProperties; constant/physicalProperties.water; constant/physicalProperties.air", of_test, of_run, "SWE-L3D-VOF", "accepted_baseline"),
        row("pressure-velocity correction", "Incompressibility coupling", "PIMPLE/PISO-like p_rgh correction and flux consistency", "adopted OpenFOAM component", "OpenFOAM Foundation 11 incompressibleVoF", "tools/openfoam/openfoam_replay.py", "system/fvSolution; 0/p_rgh", of_test, of_run, "SWE-L3D-PRS", "accepted_baseline"),
        row("k transport", "Turbulent kinetic energy", "k equation in k-omega SST RAS model", "adopted OpenFOAM component", "OpenFOAM Foundation 11 kOmegaSST", "tools/openfoam/openfoam_replay.py", "constant/momentumTransport; 0/k", of_test, of_run, "SWE-L3D-SST", "accepted_baseline"),
        row("omega transport", "Specific dissipation rate", "omega equation in k-omega SST RAS model", "adopted OpenFOAM component", "OpenFOAM Foundation 11 kOmegaSST", "tools/openfoam/openfoam_replay.py", "constant/momentumTransport; 0/omega", of_test, of_run, "SWE-L3D-SST", "accepted_baseline"),
        row("turbulent viscosity", "RAS eddy viscosity", "nut from k-omega SST closure", "adopted OpenFOAM component", "OpenFOAM Foundation 11 kOmegaSST", "tools/openfoam/openfoam_replay.py", "constant/momentumTransport; 0/nut", of_test, of_run, "SWE-L3D-SST", "accepted_baseline"),
        row("wall functions", "Near-wall turbulence closure", "kqRWallFunction, omegaWallFunction and nutkWallFunction on wall patches", "configuration of adopted backend", "repository dictionaries consumed by OpenFOAM", "tools/openfoam/openfoam_replay.py", "0/k; 0/omega; 0/nut", of_test, of_run, "SWE-L3D-WLF", "partial", "Needs y+ evidence, applicability, roughness policy, mesh-resolution consistency and load sensitivity."),
        row("inlet boundary", "Coupling-forced local inlet", "timeVaryingMappedFixedValue U and alpha.water from boundaryData", "configuration of adopted backend", "repository dictionaries and boundaryData consumed by OpenFOAM", "tools/openfoam/openfoam_replay.py", "0/U; 0/alpha.water; constant/boundaryData/inlet", of_test, of_run, "SWE-L3D-BC", "accepted_baseline"),
        row("outlet boundary", "Downstream local outflow", "inletOutlet U/alpha and fixedFluxPressure p_rgh", "configuration of adopted backend", "repository dictionaries consumed by OpenFOAM", "tools/openfoam/openfoam_replay.py", "0/U; 0/alpha.water; 0/p_rgh", of_test, of_run, "SWE-L3D-BC", "accepted_baseline"),
        row("lateral boundary", "Side boundary policy", "symmetryPlane in current smoke/replay mode", "configuration of adopted backend", "repository dictionaries consumed by OpenFOAM", "tools/openfoam/openfoam_replay.py", "0/* side patches", of_test, of_run, "SWE-L3D-BC", "partial", "Needs production lateral open-ocean policy and reflection evidence."),
        row("atmosphere boundary", "Open top boundary", "pressureInletOutletVelocity/prghTotalPressure/inletOutlet alpha", "configuration of adopted backend", "repository dictionaries consumed by OpenFOAM", "tools/openfoam/openfoam_replay.py", "0/U; 0/alpha.water; 0/p_rgh", of_test, of_run, "SWE-L3D-BC", "accepted_baseline"),
        row("terrain wall", "Impermeable bed/terrain", "noSlip/fixedFluxPressure/zeroGradient wall fields", "configuration of adopted backend", "repository dictionaries consumed by OpenFOAM", "tools/openfoam/openfoam_replay.py", "0/* terrain patches", of_test, of_run, "SWE-L3D-BC", "accepted_baseline"),
        row("barrier wall", "Rigid wall obstacle", "noSlip fixed rigid barrier patch with force function object", "configuration of adopted backend", "repository dictionaries consumed by OpenFOAM", "tools/openfoam/openfoam_replay.py", "system/blockMeshDict; system/controlDict", of_test, of_run, "SWE-L3D-FRC", "accepted_baseline"),
        row("time-step control", "Adaptive local timestep", "adjustTimeStep with maxCo, maxAlphaCo and maxDeltaT", "configuration of adopted backend", "repository dictionaries consumed by OpenFOAM", "tools/openfoam/openfoam_replay.py; tools/cases/kamaishi_delivery.py", "system/controlDict; generated replay_config.json", of_test, of_run, "SWE-L3D-TIM", "partial", "Needs diffusion constraint evidence and rejected-step/recovery disposition."),
        row("force and moment extraction", "Barrier load extraction", "OpenFOAM forces function object over barrier patch", "configuration of adopted backend", "repository dictionaries consumed by OpenFOAM", "tools/openfoam/openfoam_replay.py", "system/controlDict forces function", of_test, of_run, "SWE-L3D-FRC", "accepted_baseline", "Surface pressure/shear distributions, traction vectors, impulse and load convergence remain for full issue closure."),
    ]


def remaining_gaps() -> list[dict[str, str]]:
    return [
        {
            "gap_id": "G6-L3D-BC-001",
            "wbs_id": "SWE-L3D-BC",
            "description": "Define the production Local3D lateral open-ocean boundary policy instead of relying only on symmetryPlane test-mode sides.",
            "why_it_blocks_g6": "The G6 Local3D model requires a baseline lateral boundary policy suitable for the theoretical hybrid model, not only a smoke-test symmetry mode.",
            "current_evidence": "tools/openfoam/openfoam_replay.py writes symmetryPlane side patches; PR #270/#271 prove runtime acceptance for that mode.",
            "required_change": "Add a documented production lateral boundary configuration or an explicit accepted policy limiting G6 use to symmetry-side geometry.",
            "required_test": "Automated dictionary/acceptance test that distinguishes production open-ocean mode from symmetry test mode.",
            "required_acceptance": "No ambiguity remains between test symmetry and production lateral-open boundary handling.",
            "estimated_scope": "small-to-medium documentation and case-generation change",
        },
        {
            "gap_id": "G6-L3D-BC-002",
            "wbs_id": "SWE-L3D-BC",
            "description": "Provide boundary-reflection evidence for the selected Local3D outlet/lateral treatment.",
            "why_it_blocks_g6": "The theoretical hybrid gate needs evidence that the baseline boundary policy does not invalidate the replayed local model at the interface scale.",
            "current_evidence": "Runtime boundedness and final-time acceptance exist, but no reflection diagnostic or benchmark is recorded.",
            "required_change": "Add a lightweight reflection diagnostic/benchmark or an evidence note with accepted limits.",
            "required_test": "Focused boundary-condition benchmark or diagnostic assertion over a known replay/synthetic signal.",
            "required_acceptance": "Reflection behavior is measured and judged acceptable for G6 baseline use.",
            "estimated_scope": "medium",
        },
        {
            "gap_id": "G6-L3D-WLF-001",
            "wbs_id": "SWE-L3D-WLF",
            "description": "Record baseline wall-function applicability evidence, including y+ or an approved surrogate rationale.",
            "why_it_blocks_g6": "Wall functions are part of the Local3D theoretical baseline; the repository currently configures them but does not evidence applicability.",
            "current_evidence": "Generated OpenFOAM fields use kqRWallFunction, omegaWallFunction and nutkWallFunction and runs complete.",
            "required_change": "Add a wall-function applicability note and compute or explicitly bound y+ expectations for the generated baseline.",
            "required_test": "Automated check that wall-function policy evidence exists for generated cases or that the selected mesh policy reports a valid surrogate.",
            "required_acceptance": "G6 reviewers can see why the baseline wall treatment is acceptable before calibration.",
            "estimated_scope": "small-to-medium",
        },
        {
            "gap_id": "G6-L3D-TIM-001",
            "wbs_id": "SWE-L3D-TIM",
            "description": "Formally dispose of timestep rejection/recovery and diffusion-constraint coverage under the OpenFOAM adopted backend.",
            "why_it_blocks_g6": "The WBS title includes CFL, interface-CFL, gravity-wave and diffusion constraints with rejection; OpenFOAM owns internal adaptation and the repository only configures maxCo/maxAlphaCo/maxDeltaT today.",
            "current_evidence": "Kamaishi delivery derives maxDeltaT from flow speed, gravity wave speed and mesh spacing, and PR #271 records bounded Courant values.",
            "required_change": "Document which constraints are repository-controlled, which are OpenFOAM-controlled, and what rejected-step recovery is or is not exposed.",
            "required_test": "Focused validator for generated timestep policy and an acceptance note for rejected-step limitations.",
            "required_acceptance": "The G6 gate can distinguish implemented baseline timestep control from post-G6 convergence study obligations.",
            "estimated_scope": "small",
        },
    ]


def write_csv(path: Path, rows: list[dict[str, Any]], columns: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        for row in rows:
            writer.writerow({column: row.get(column, "") for column in columns})


def write_markdown_table(path: Path, rows: list[dict[str, str]], title: str) -> None:
    columns = list(rows[0].keys()) if rows else []
    with path.open("w", encoding="utf-8") as handle:
        handle.write(f"# {title}\n\n")
        handle.write("| " + " | ".join(columns) + " |\n")
        handle.write("| " + " | ".join("---" for _ in columns) + " |\n")
        for row in rows:
            values = [str(row[column]).replace("\n", "<br>").replace("|", "\\|") for column in columns]
            handle.write("| " + " | ".join(values) + " |\n")


def write_report(inventory: list[dict[str, Any]], hierarchy: dict[str, Any], ledger: list[dict[str, Any]], gaps: list[dict[str, str]]) -> None:
    disposition_counts = defaultdict(int)
    gate_counts = defaultdict(int)
    for row in ledger:
        disposition_counts[row["recommended_disposition"]] += 1
        gate_counts[row["gate_relevance"]] += 1
    adopted = [row for row in ledger if row["recommended_disposition"] == "complete_adopted_backend"]
    native = [row for row in ledger if row["recommended_disposition"] == "complete_native"]
    partial = [row for row in ledger if row["recommended_disposition"] == "partial"]
    deferred = [row for row in ledger if row["recommended_disposition"] == "deferred"]

    path = OUT_DIR / "wbs_reconciliation_report.md"
    with path.open("w", encoding="utf-8") as handle:
        handle.write("# WBS Reconciliation Report\n\n")
        handle.write("Baseline: `1ba6475 feat(g5): add Kamaishi hybrid delivery pipeline (#271)`.\n\n")
        handle.write("This audit reconciles the GitHub WBS against the implemented real-data hybrid execution through the adopted OpenFOAM Foundation 11 Local3D backend. Calibration was not started and the numerical model was not modified.\n\n")
        handle.write("## Inventory\n\n")
        handle.write(f"- WBS issues audited: {len(inventory)}\n")
        handle.write(f"- Hierarchy levels reconstructed: {hierarchy['hierarchy_levels']}\n")
        handle.write(f"- G6-required issues/items: {gate_counts['g6_required']}\n")
        handle.write(f"- Post-G6 issues/items: {gate_counts['post_g6']}\n")
        handle.write(f"- Excluded from current scope: {gate_counts['excluded_from_current_scope']}\n\n")
        handle.write("## Disposition Summary\n\n")
        for key in sorted(VALID_DISPOSITIONS):
            handle.write(f"- `{key}`: {disposition_counts[key]}\n")
        handle.write("\n")
        handle.write("## OpenFOAM Adoption\n\n")
        handle.write("OpenFOAM Foundation 11 is treated as an adopted numerical backend for the Local3D incompressible two-phase URANS-VOF baseline. The repository owns the coupling reconstruction, generated dictionaries, execution wrapper, acceptance validation, extraction and provenance. The backend owns VOF transport, momentum discretisation, pressure-velocity coupling, k-omega SST transport, wall-function evaluation and transient solver sequencing.\n\n")
        handle.write(f"Decision record: `{DECISION_PATH}`.\n\n")
        handle.write("## Issues To Close As Adopted Backend\n\n")
        handle.write(bullet([f"#{row['issue_number']} `{row['wbs_id']}` {row['title']}" for row in adopted]) + "\n\n")
        handle.write("## Issues To Close As Native Coupling\n\n")
        coupling_native = [row for row in native if row["issue_number"] in COUPLING_CLOSE]
        handle.write(bullet([f"#{row['issue_number']} `{row['wbs_id']}` {row['title']}" for row in coupling_native]) + "\n\n")
        handle.write("## Issues Retained As Partial\n\n")
        priority_partial = [row for row in partial if row["issue_number"] in set(LOCAL_PARTIAL) | set(COUPLING_PARTIAL) | set(PARENT_STATUS)]
        handle.write(bullet([f"#{row['issue_number']} `{row['wbs_id']}`: {', '.join(row['remaining_acceptance'])}" for row in priority_partial]) + "\n\n")
        handle.write("## Deferred Or Post-G6 Scope\n\n")
        handle.write("- GUI remains deferred and excluded from the current gate.\n")
        handle.write("- HPC remains open and post-G6 for MPI scaling, CPU affinity and GPU feasibility.\n")
        handle.write("- HDF5/XDMF, convergence, validation, FSI/scour/structural damage, GUI and publication outputs are excluded from G6.\n\n")
        handle.write("## Remaining G6 Blockers\n\n")
        for gap in gaps:
            handle.write(f"- `{gap['gap_id']}` ({gap['wbs_id']}): {gap['description']}\n")
        handle.write("\n")
        handle.write("## Verification\n\n")
        handle.write("- `python3 -m json.tool <every-new-json-file>`\n")
        handle.write("- `python3 tools/project-management/validate_wbs_reconciliation.py`\n")
        handle.write("- `git diff --check`\n")


def write_gate_doc(gaps: list[dict[str, str]]) -> None:
    path = OUT_DIR / "g6_theoretical_model_gate.md"
    with path.open("w", encoding="utf-8") as handle:
        handle.write("# G6 — Theoretical Hybrid Model Complete\n\n")
        handle.write("Suggested GitHub issue title: `[G6] Theoretical 2D-3D hybrid model complete`.\n\n")
        handle.write("## Gate Rule\n\n")
        handle.write("G6 is blocked until every required theoretical-model capability has accepted implementation evidence and each remaining G6 gap below is resolved. Calibration, observational validation and convergence studies are explicitly outside this gate.\n\n")
        handle.write("## Required Capabilities\n\n")
        for section, items in {
            "Regional2D mathematical model": ["depth-averaged NLSWE", "bathymetry/topography", "wet/dry treatment", "gravity", "Manning friction", "numerical flux and time integration", "Regional2D boundaries", "runtime physical acceptance"],
            "Earthquake source": ["real USGS finite-fault input", "projected-grid displacement generation", "vertical seabed displacement", "passive free-surface transfer", "source provenance"],
            "Geospatial corridor": ["epicentre", "Kamaishi target/interface", "projected CRS", "corridor construction", "terrain conditioning", "tagged mesh", "nearshore interface"],
            "Coupling": ["versioned regional export", "coordinate and datum conventions", "spatial mapping", "temporal handling", "3D inlet reconstruction", "normal and tangential velocity", "free-surface/alpha reconstruction", "dry treatment", "discharge preservation"],
            "Local3D mathematical model": ["incompressible immiscible two-phase Navier-Stokes", "VOF free surface", "URANS", "k-omega SST", "pressure-velocity coupling", "wall functions", "coupling-enabled inlet", "outlet", "lateral boundary policy", "atmospheric top", "terrain and barrier walls", "adaptive timestep/CFL controls"],
            "End-to-end execution": ["real-data Regional2D run", "coupling export", "complete replay window", "no-defence Local3D run", "rigid-wall Local3D run", "finite and bounded fields", "force and probe output", "reproducible command"],
        }.items():
            handle.write(f"### {section}\n\n")
            handle.write(bullet(items) + "\n\n")
        handle.write("## Exclusions\n\n")
        handle.write(bullet([
            "observational calibration",
            "observation validation",
            "mesh-convergence study",
            "timestep-convergence study",
            "HDF5 result container",
            "XDMF/VTKHDF visualisation layer",
            "CPU/GPU acceleration",
            "adaptive mesh refinement",
            "official CAD barrier geometry",
            "buildings",
            "full impact metric suite",
            "obstacle/dissipating barrier comparison",
            "GUI",
            "FSI",
            "scour",
            "structural damage",
            "machine learning",
            "publication figures",
        ]) + "\n\n")
        handle.write("## Remaining G6 Gaps\n\n")
        for gap in gaps:
            handle.write(f"- `{gap['gap_id']}`: {gap['description']}\n")


def write_project_update(ledger: list[dict[str, Any]]) -> None:
    path = OUT_DIR / "github_project_update.md"
    fields = ["Implementation Route", "Evidence State", "WBS Disposition", "Gate Relevance", "Current Gate"]
    with path.open("w", encoding="utf-8") as handle:
        handle.write("# GitHub Project Update\n\n")
        handle.write("## Discovery Result\n\n")
        handle.write("- Repository: `Summer-Studentship/Summer-Studentship`\n")
        handle.write("- Classic repository Projects endpoint: `404 Not Found`.\n")
        handle.write("- Local `gh auth status`: token for `Helios-MEOW` has `repo` scope and can write issues, milestones and PRs through the GitHub CLI.\n")
        handle.write("- Current GitHub connector exposes issues, branches, commits and PRs, but returned `403 Resource not accessible by integration` for issue and PR writes in this session.\n")
        handle.write("- `gh project` is installed, but Project commands require an OAuth Project scope not available in this session.\n")
        handle.write("- Repository convention in `docs/project-management/manual-wbs-workflow.md` says Project fields are completed manually.\n\n")
        handle.write("Project owner, Project number and Project URL were not discoverable through the available authenticated tools. Do not claim a saved view was created from this prompt.\n\n")
        handle.write("## Fields To Create Or Reuse\n\n")
        handle.write("- `Implementation Route`: Native, OpenFOAM adopted, OpenFOAM adapted, External library, Deferred\n")
        handle.write("- `Evidence State`: No evidence, Implementation linked, Acceptance passed, Validation passed\n")
        handle.write("- `WBS Disposition`: Complete, Partial, Superseded, Deferred, Not started\n")
        handle.write("- `Gate Relevance`: G6 required, Post-G6, Excluded\n")
        handle.write("- `Current Gate`: text or single select; use `G6 — Theoretical Hybrid Model Complete` for G6-required items\n\n")
        handle.write("## Saved View Manual Instructions\n\n")
        handle.write("Create or update a Project view named `G6 — Theoretical Model Gate` with:\n\n")
        handle.write("- Filter: `Gate Relevance = G6 required`\n")
        handle.write("- Group: parent WBS or workstream field\n")
        handle.write("- Sort: `WBS Disposition`, then `WBS ID`\n")
        handle.write("- Visible fields: Status, WBS ID, WBS Disposition, Implementation Route, Evidence State, Gate Relevance\n\n")
        handle.write("## Item Field Values\n\n")
        handle.write("| Issue | WBS ID | " + " | ".join(fields) + " |\n")
        handle.write("| --- | --- | " + " | ".join("---" for _ in fields) + " |\n")
        for row in ledger:
            values = [row["project_field_changes"].get(field, "") for field in fields]
            handle.write(f"| #{row['issue_number']} | `{row['wbs_id']}` | " + " | ".join(values) + " |\n")


def write_decision() -> None:
    DECISION_PATH.parent.mkdir(parents=True, exist_ok=True)
    DECISION_PATH.write_text(
        """# OpenFOAM Local3D Backend Adoption

Date: 2026-08-06

Status: Accepted for the G6 theoretical hybrid model baseline

## Decision

OpenFOAM Foundation 11 is the numerical implementation authority for the
Local3D incompressible two-phase URANS-VOF baseline.

OpenFOAM is an adopted backend, not the governing research authority. The
research model, interface contracts and accepted formulation remain
authoritative for the studentship. Repository evidence must continue to show
how each mathematical component maps either to repository-native code, adopted
OpenFOAM behavior, or an external geospatial/scientific library.

## Repository Responsibilities

- coupling reconstruction;
- case generation;
- model selection;
- boundary-condition definition;
- execution orchestration;
- acceptance validation;
- result extraction;
- provenance.

## OpenFOAM Responsibilities

- VOF transport;
- momentum discretisation;
- pressure-velocity coupling;
- k-omega SST transport;
- wall-function evaluation;
- transient solver sequence.

## Advantages

- Provides a mature, inspectable implementation of incompressible two-phase
  URANS-VOF behavior without adding an unvalidated repository-native Local3D
  solver.
- Keeps the repository focused on the novel hybrid contract: source, regional
  propagation, coupling reconstruction, case generation, validation and
  provenance.
- Enables reproducible smoke and real-data runs through a pinned container image.
- Separates accepted backend behavior from research authority, so future model
  changes remain explicit.

## Limitations

- OpenFOAM internals are not repository-native code and must not be described as
  such in WBS closure evidence.
- Repository control over rejected-step recovery is limited to generated
  dictionaries, runtime log inspection and documented backend behavior.
- Current lateral side treatment uses symmetryPlane in the replay baseline and
  still needs production open-ocean policy evidence.
- Wall-function applicability requires y+ or approved surrogate evidence before
  the G6 gate can close.
- The current rigid wall is a simplified baseline, not an official CAD or full
  barrier-class comparison.

## Verification Obligations

- Record OpenFOAM image, digest and Foundation version for accepted runs.
- Validate generated dictionaries, field set, boundaryData coverage and
  function-object outputs.
- Reject runs with fatal OpenFOAM errors, floating-point exceptions, nonfinite
  final fields, incomplete replay windows, missing probe output or missing force
  output where required.
- Maintain theory-to-implementation traceability for every G6 mathematical row.

## Validation Obligations

Observation calibration, observation validation, mesh convergence, timestep
convergence and approved impact/load validation remain post-G6 work. They must
not be claimed as complete by backend adoption alone.

## Replacement Conditions

Replace or supplement the OpenFOAM backend if:

- the accepted research formulation diverges materially from available
  OpenFOAM behavior;
- required boundary, wall, turbulence or coupling evidence cannot be produced;
- licensing, reproducibility or container availability becomes unacceptable;
- a repository-native Local3D solver reaches equal or stronger acceptance
  evidence.

## Version Pinning

The accepted baseline uses:

- image: `docker.io/openfoam/openfoam11-paraview510:11`;
- observed digest from PR #270: `sha256:fd10956e0b1eb70f9808baf2857e4baf846a0f6f272f73b6d00546eae96be181`;
- observed image id from PR #270:
  `7f8a8af7c4c5884a41a61e42f5a18e037f46a114ae91196f4154a4cdac1e4f93`;
- solver: `foamRun -solver incompressibleVoF`;
- turbulence model: `kOmegaSST` in `constant/momentumTransport`.
""",
        encoding="utf-8",
    )


def write_outputs(inventory: list[dict[str, Any]], prs: dict[int, dict[str, Any]]) -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    hierarchy = build_hierarchy(inventory)
    ledger = build_ledger(inventory)
    gaps = remaining_gaps()
    matrix = traceability_rows()
    plan = mutation_plan(inventory, ledger)
    result = {
        "generated_at_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "status": "planned_not_applied",
        "applied_issue_mutations": [],
        "unsupported_mutations": [
            {
                "kind": "milestone_create",
                "target": "G6 — Theoretical Hybrid Model Complete",
                "reason": "The dry-run generator does not create milestones; apply_wbs_g6_mutations.py applies this live through GitHub CLI when write auth is available.",
            },
            {
                "kind": "projects_v2_fields_and_view",
                "target": "WBS GitHub Project",
                "reason": "Projects V2 field/item/saved-view mutation requires GitHub Project OAuth scope; repository convention records manual Project-field updates.",
            },
        ],
        "notes": "Updated after live GitHub issue mutations are applied.",
    }

    inventory_payload = {
        "generated_at_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "repository": REPO,
        "source_baseline": "1ba6475 feat(g5): add Kamaishi hybrid delivery pipeline (#271)",
        "comment_fetch_note": "Comments are fetched where API budget allowed; comments_count records live GitHub counts for every WBS issue.",
        "pull_requests_seen_in_issue_pages": {
            str(number): {
                "title": item.get("title"),
                "state": item.get("state"),
                "html_url": item.get("html_url"),
                "merged_at": (item.get("pull_request") or {}).get("merged_at"),
                "body": item.get("body"),
            }
            for number, item in sorted(prs.items())
        },
        "issues": inventory,
    }

    (OUT_DIR / "github_issue_inventory.json").write_text(json.dumps(inventory_payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (OUT_DIR / "wbs_hierarchy.json").write_text(json.dumps(hierarchy, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (OUT_DIR / "wbs_evidence_ledger.json").write_text(json.dumps(ledger, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (OUT_DIR / "g6_remaining_gaps.json").write_text(json.dumps(gaps, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (OUT_DIR / "wbs_mutation_plan.json").write_text(json.dumps(plan, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (OUT_DIR / "wbs_mutation_result.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    ledger_csv_rows = []
    for row in ledger:
        ledger_csv_rows.append(
            {
                "wbs_id": row["wbs_id"],
                "issue_number": row["issue_number"],
                "title": row["title"],
                "level": row["level"],
                "parent_wbs": row["parent_wbs"],
                "current_state": row["current_state"],
                "recommended_disposition": row["recommended_disposition"],
                "gate_relevance": row["gate_relevance"],
                "implementation_route": row["implementation_route"],
                "evidence": "; ".join(row["evidence"]),
                "remaining_acceptance": "; ".join(row["remaining_acceptance"]),
                "recommended_issue_action": row["recommended_issue_action"],
            }
        )
    write_csv(
        OUT_DIR / "wbs_evidence_ledger.csv",
        ledger_csv_rows,
        [
            "wbs_id",
            "issue_number",
            "title",
            "level",
            "parent_wbs",
            "current_state",
            "recommended_disposition",
            "gate_relevance",
            "implementation_route",
            "evidence",
            "remaining_acceptance",
            "recommended_issue_action",
        ],
    )

    matrix_columns = [
        "model_component",
        "mathematical_role",
        "governing_equation_or_condition",
        "implementation_route",
        "implementation_authority",
        "repository_location",
        "configuration_location",
        "test_evidence",
        "run_evidence",
        "wbs_id",
        "g6_status",
        "remaining_gap",
    ]
    write_csv(OUT_DIR / "g6_model_traceability_matrix.csv", matrix, matrix_columns)
    write_markdown_table(OUT_DIR / "g6_model_traceability_matrix.md", matrix, "G6 Model Traceability Matrix")
    write_report(inventory, hierarchy, ledger, gaps)
    write_gate_doc(gaps)
    write_project_update(ledger)
    write_decision()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--max-comment-fetches", type=int, default=30)
    args = parser.parse_args(argv)
    try:
        inventory, prs = fetch_inventory(args.max_comment_fetches)
    except Exception as exc:
        print(f"failed to fetch inventory: {exc}", file=sys.stderr)
        return 1
    write_outputs(inventory, prs)
    print(f"wrote reconciliation artifacts for {len(inventory)} WBS issues")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
