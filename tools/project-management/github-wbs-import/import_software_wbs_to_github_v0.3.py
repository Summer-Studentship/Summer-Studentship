#!/usr/bin/env python3
"""
Import the Studentship Software WBS into GitHub.

Creates, idempotently:
- organization issue types;
- organization issue fields;
- G0-G5 milestones;
- WBS issues from the JSON manifest;
- native Parent/Sub-issue relationships;
- native Blocked-by dependencies;
- an organization Project and links the issue fields to it;
- Project items for every created/reused issue.

Requirements:
- Python 3.10+
- Standard library only
- GITHUB_TOKEN with:
  * repository Issues: write
  * organization Issue Types: write
  * organization Issue Fields: write
  * Projects: write
  * organization administration privileges for issue type/field creation
- The token/app must be installed or authorised for the target organization/repository.

The script is idempotent by WBS ID and reuses existing organization schema,
milestones, Projects and issues. It will reuse an issue whose title begins
with "[<WBS ID>]".
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Iterable

API = "https://api.github.com"
GRAPHQL = "https://api.github.com/graphql"
API_VERSION = "2026-03-10"


class GitHubError(RuntimeError):
    pass


class GitHub:
    def __init__(self, token: str, *, apply: bool, delay: float = 1.05) -> None:
        self.token = token
        self.apply = apply
        self.delay = delay

    def _request(
        self,
        method: str,
        url: str,
        payload: dict[str, Any] | list[Any] | None = None,
        *,
        expected: Iterable[int] = (200,),
    ) -> Any:
        if not self.apply and method not in {"GET", "HEAD"}:
            print(f"DRY-RUN {method} {url}")
            if payload is not None:
                print(json.dumps(payload, indent=2))
            return {}

        data = None if payload is None else json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            url,
            data=data,
            method=method,
            headers={
                "Accept": "application/vnd.github+json",
                "Authorization": f"Bearer {self.token}",
                "X-GitHub-Api-Version": API_VERSION,
                "User-Agent": "studentship-wbs-importer/0.3",
                "Content-Type": "application/json",
            },
        )
        try:
            with urllib.request.urlopen(req, timeout=60) as response:
                raw = response.read()
                status = response.status
                if status not in set(expected):
                    raise GitHubError(f"{method} {url}: unexpected HTTP {status}")
                result = json.loads(raw) if raw else None
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="replace")
            raise GitHubError(
                f"{method} {url}: HTTP {exc.code}\n{body}"
            ) from exc
        time.sleep(self.delay)
        return result

    def rest(
        self,
        method: str,
        path: str,
        payload: dict[str, Any] | list[Any] | None = None,
        *,
        expected: Iterable[int] = (200,),
    ) -> Any:
        return self._request(method, f"{API}{path}", payload, expected=expected)

    def graphql(self, query: str, variables: dict[str, Any]) -> dict[str, Any]:
        result = self._request(
            "POST",
            GRAPHQL,
            {"query": query, "variables": variables},
            expected=(200,),
        )
        if not self.apply:
            return {}
        if result.get("errors"):
            raise GitHubError(json.dumps(result["errors"], indent=2))
        return result["data"]

    def paged(self, path: str) -> list[dict[str, Any]]:
        items: list[dict[str, Any]] = []
        page = 1
        separator = "&" if "?" in path else "?"
        while True:
            batch = self.rest("GET", f"{path}{separator}per_page=100&page={page}")
            if not batch:
                break
            items.extend(batch)
            if len(batch) < 100:
                break
            page += 1
        return items


def split_repo(repo: str) -> tuple[str, str]:
    try:
        owner, name = repo.split("/", 1)
    except ValueError as exc:
        raise SystemExit("--repo must use OWNER/REPOSITORY") from exc
    if not owner or not name:
        raise SystemExit("--repo must use OWNER/REPOSITORY")
    return owner, name


def load_manifest(path: Path) -> list[dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, list):
        raise SystemExit("Manifest must be a JSON array.")
    required = {"WBS ID", "Level", "Issue Title", "Parent WBS ID", "Gate", "Scope Class", "Body"}
    for index, row in enumerate(data):
        missing = required - set(row)
        if missing:
            raise SystemExit(f"Manifest row {index} is missing: {sorted(missing)}")
    return data


ISSUE_TYPES = [
    ("Workstream", "Top-level implementation workstream.", "purple"),
    ("Domain", "A coherent Software ownership domain.", "blue"),
    ("Deliverable", "An accepted WBS output with closure evidence.", "green"),
    ("Work Package", "A bounded implementation package within a Deliverable.", "yellow"),
    ("Task", "An atomic implementation or verification action.", "gray"),
    ("Bug", "A confirmed defect.", "red"),
    ("Decision", "A tracked technical or project decision.", "pink"),
]

ISSUE_FIELDS = [
    {
        "name": "WBS ID",
        "description": "Canonical Studentship WBS identifier.",
        "data_type": "text",
    },
    {
        "name": "Scope Class",
        "description": "Baseline, continuous, stretch or deferred scope.",
        "data_type": "single_select",
        "options": [
            {"name": "Active baseline", "description": "Required active scope.", "color": "green"},
            {"name": "Continuous baseline", "description": "Continuous supporting scope.", "color": "blue"},
            {"name": "Stretch", "description": "Optional extension after the baseline.", "color": "yellow"},
            {"name": "Deferred", "description": "Explicitly outside the current baseline.", "color": "gray"},
        ],
    },
    {
        "name": "Priority",
        "description": "Implementation priority.",
        "data_type": "single_select",
        "options": [
            {"name": "Critical", "description": "Critical path.", "color": "red"},
            {"name": "High", "description": "High priority.", "color": "orange"},
            {"name": "Medium", "description": "Normal priority.", "color": "yellow"},
            {"name": "Low", "description": "Low priority.", "color": "gray"},
        ],
    },
    {
        "name": "Risk",
        "description": "Current delivery or technical risk.",
        "data_type": "single_select",
        "options": [
            {"name": "High", "description": "High risk.", "color": "red"},
            {"name": "Medium", "description": "Medium risk.", "color": "yellow"},
            {"name": "Low", "description": "Low risk.", "color": "green"},
        ],
    },
    {
        "name": "Research Inputs",
        "description": "Authoritative RES WBS inputs.",
        "data_type": "text",
    },
    {
        "name": "Target Gate",
        "description": "Software integration gate.",
        "data_type": "single_select",
        "options": [
            *[
                {"name": f"G{i}", "description": f"Gate G{i}.", "color": "blue"}
                for i in range(6)
            ],
            {"name": "Continuous", "description": "Continuous delivery.", "color": "green"},
            {"name": "Post-G5", "description": "After active baseline.", "color": "gray"},
            {"name": "G0/G1", "description": "Spans initial gates.", "color": "purple"},
            {"name": "G0 onward", "description": "Begins at G0 and continues.", "color": "purple"},
            {"name": "G0-G5", "description": "Workstream gate range.", "color": "purple"},
        ],
    },
    {
        "name": "Effort",
        "description": "Estimated effort points.",
        "data_type": "number",
    },
    {
        "name": "Definition Status",
        "description": "Maturity of the WBS definition.",
        "data_type": "single_select",
        "options": [
            {"name": "Proposed", "description": "Requires acceptance.", "color": "gray"},
            {"name": "Defined", "description": "Ready for planning.", "color": "yellow"},
            {"name": "Accepted", "description": "Authoritative baseline.", "color": "green"},
        ],
    },
    {
        "name": "Verification Required",
        "description": "Whether objective verification evidence is required.",
        "data_type": "single_select",
        "options": [
            {"name": "Yes", "description": "Verification evidence required.", "color": "green"},
            {"name": "No", "description": "No separate verification evidence.", "color": "gray"},
        ],
    },
]


def normalize_issue_field_options() -> None:
    """Add the REST-required 1-based ordering priority to select options."""
    for field in ISSUE_FIELDS:
        if field.get("data_type") not in {"single_select", "multi_select"}:
            continue
        options = field.get("options")
        if not isinstance(options, list) or not options:
            raise GitHubError(
                f"Select issue field {field.get('name')!r} must define options."
            )
        for priority, option in enumerate(options, start=1):
            option["priority"] = priority


def validate_issue_field_definitions() -> None:
    """Fail locally before issuing organization schema mutations."""
    allowed_colors = {
        "gray", "blue", "green", "yellow",
        "orange", "red", "pink", "purple",
    }
    for field in ISSUE_FIELDS:
        data_type = field.get("data_type")
        if data_type not in {"single_select", "multi_select"}:
            continue

        options = field.get("options", [])
        priorities = []
        names = set()
        for option in options:
            missing = {"name", "color", "priority"} - set(option)
            if missing:
                raise GitHubError(
                    f"Issue field {field['name']!r} option is missing "
                    f"required keys: {sorted(missing)}"
                )
            if option["name"] in names:
                raise GitHubError(
                    f"Issue field {field['name']!r} has duplicate option "
                    f"{option['name']!r}."
                )
            names.add(option["name"])

            if option["color"] not in allowed_colors:
                raise GitHubError(
                    f"Issue field {field['name']!r} uses invalid colour "
                    f"{option['color']!r}."
                )
            if not isinstance(option["priority"], int) or option["priority"] < 1:
                raise GitHubError(
                    f"Issue field {field['name']!r} has invalid priority "
                    f"{option['priority']!r}."
                )
            priorities.append(option["priority"])

        expected = list(range(1, len(options) + 1))
        if priorities != expected:
            raise GitHubError(
                f"Issue field {field['name']!r} priorities must be "
                f"sequential and 1-based: expected {expected}, got {priorities}."
            )


normalize_issue_field_options()

MILESTONES = [
    ("G0", "Repository and Build Baseline"),
    ("G1", "Data and Mesh Vertical Slice"),
    ("G2", "Verified Regional 2D Baseline"),
    ("G3", "Verified Local 3D Baseline"),
    ("G4", "Regional-to-Local Replay"),
    ("G5", "Barrier Impact Comparison Baseline"),
]


def ensure_issue_types(gh: GitHub, org: str) -> dict[str, dict[str, Any]]:
    current = {x["name"]: x for x in gh.rest("GET", f"/orgs/{org}/issue-types")}
    for name, description, color in ISSUE_TYPES:
        if name in current:
            continue
        created = gh.rest(
            "POST",
            f"/orgs/{org}/issue-types",
            {"name": name, "description": description, "is_enabled": True, "color": color},
            expected=(200,),
        )
        if gh.apply:
            current[name] = created
    return current


def ensure_issue_fields(gh: GitHub, org: str) -> dict[str, dict[str, Any]]:
    current = {x["name"]: x for x in gh.rest("GET", f"/orgs/{org}/issue-fields")}
    for definition in ISSUE_FIELDS:
        name = definition["name"]
        if name in current:
            continue
        created = gh.rest(
            "POST",
            f"/orgs/{org}/issue-fields",
            definition,
            expected=(200,),
        )
        if gh.apply:
            current[name] = created
    return current


def ensure_milestones(gh: GitHub, owner: str, repo: str) -> dict[str, int]:
    current = {
        x["title"]: x["number"]
        for x in gh.paged(f"/repos/{owner}/{repo}/milestones?state=all")
    }
    for title, description in MILESTONES:
        if title in current:
            continue
        created = gh.rest(
            "POST",
            f"/repos/{owner}/{repo}/milestones",
            {"title": title, "description": description, "state": "open"},
            expected=(201,),
        )
        if gh.apply:
            current[title] = created["number"]
    return current


def existing_wbs_issues(gh: GitHub, owner: str, repo: str) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for issue in gh.paged(f"/repos/{owner}/{repo}/issues?state=all"):
        if "pull_request" in issue:
            continue
        match = re.match(r"^\[([A-Z0-9-]+)\]", issue.get("title", ""))
        if match:
            result[match.group(1)] = issue
    return result


def field_value_payload(
    row: dict[str, Any],
    fields: dict[str, dict[str, Any]],
) -> list[dict[str, Any]]:
    values: list[dict[str, Any]] = []

    def put(name: str, value: Any) -> None:
        if value in (None, "", []):
            return
        field = fields.get(name)
        if field:
            values.append({"field_id": field["id"], "value": value})

    put("WBS ID", row["WBS ID"])
    put("Scope Class", row.get("Scope Class"))
    put("Research Inputs", row.get("Research Inputs"))
    put("Target Gate", row.get("Gate"))
    put("Definition Status", "Accepted" if row["Level"] in {"Workstream", "Domain", "Deliverable"} else "Defined")
    put("Verification Required", "Yes")
    return values


def ensure_issues(
    gh: GitHub,
    owner: str,
    repo: str,
    rows: list[dict[str, Any]],
    issue_fields: dict[str, dict[str, Any]],
    milestones: dict[str, int],
) -> dict[str, dict[str, Any]]:
    current = existing_wbs_issues(gh, owner, repo)
    for index, row in enumerate(rows, start=1):
        wbs_id = row["WBS ID"]
        if wbs_id in current:
            print(f"REUSE {wbs_id} -> #{current[wbs_id]['number']}")
            continue

        gate = row.get("Gate", "")
        milestone = milestones.get(gate)
        payload: dict[str, Any] = {
            "title": row["Issue Title"],
            "body": row["Body"],
            "type": row["Level"],
            "issue_field_values": field_value_payload(row, issue_fields),
        }
        if milestone:
            payload["milestone"] = milestone

        print(f"CREATE {index}/{len(rows)} {wbs_id}")
        created = gh.rest(
            "POST",
            f"/repos/{owner}/{repo}/issues",
            payload,
            expected=(201,),
        )
        if gh.apply:
            current[wbs_id] = created
    return current


def ensure_hierarchy(
    gh: GitHub,
    owner: str,
    repo: str,
    rows: list[dict[str, Any]],
    issues: dict[str, dict[str, Any]],
) -> None:
    for row in rows:
        parent_id = row.get("Parent WBS ID")
        if not parent_id:
            continue
        child = issues.get(row["WBS ID"])
        parent = issues.get(parent_id)
        if not child or not parent:
            raise GitHubError(f"Missing issue for hierarchy: {parent_id} -> {row['WBS ID']}")
        try:
            gh.rest(
                "POST",
                f"/repos/{owner}/{repo}/issues/{parent['number']}/sub_issues",
                {"sub_issue_id": child["id"]},
                expected=(201,),
            )
            print(f"PARENT {parent_id} -> {row['WBS ID']}")
        except GitHubError as exc:
            if "422" not in str(exc):
                raise
            print(f"SKIP existing/invalid parent relation {parent_id} -> {row['WBS ID']}")


def ensure_dependencies(
    gh: GitHub,
    owner: str,
    repo: str,
    rows: list[dict[str, Any]],
    issues: dict[str, dict[str, Any]],
) -> None:
    for row in rows:
        raw = row.get("Dependencies", "")
        if not raw:
            continue
        current = issues[row["WBS ID"]]
        for blocker_wbs in [x.strip() for x in raw.split(";") if x.strip()]:
            blocker = issues.get(blocker_wbs)
            if not blocker:
                raise GitHubError(f"Missing blocker issue: {blocker_wbs}")
            try:
                gh.rest(
                    "POST",
                    f"/repos/{owner}/{repo}/issues/{current['number']}/dependencies/blocked_by",
                    {"issue_id": blocker["id"]},
                    expected=(201,),
                )
                print(f"BLOCKED {row['WBS ID']} by {blocker_wbs}")
            except GitHubError as exc:
                if "422" not in str(exc):
                    raise
                print(f"SKIP existing/invalid dependency {row['WBS ID']} <- {blocker_wbs}")


def graphql_owner_and_project(
    gh: GitHub,
    org: str,
    repo_node_id: str,
    title: str,
) -> str:
    query = """
    query($login: String!) {
      organization(login: $login) {
        id
        projectsV2(first: 100) { nodes { id title } }
      }
    }
    """
    data = gh.graphql(query, {"login": org})
    if not gh.apply:
        return "DRY_RUN_PROJECT"
    organization = data.get("organization")
    if not organization:
        raise GitHubError(f"Organization not found or inaccessible: {org}")
    for project in organization["projectsV2"]["nodes"]:
        if project["title"] == title:
            return project["id"]

    mutation = """
    mutation($ownerId: ID!, $repositoryId: ID!, $title: String!) {
      createProjectV2(input: {
        ownerId: $ownerId,
        repositoryId: $repositoryId,
        title: $title
      }) {
        projectV2 { id }
      }
    }
    """
    created = gh.graphql(
        mutation,
        {
            "ownerId": organization["id"],
            "repositoryId": repo_node_id,
            "title": title,
        },
    )
    return created["createProjectV2"]["projectV2"]["id"]


def project_field_names(gh: GitHub, project_id: str) -> set[str]:
    query = """
    query($id: ID!) {
      node(id: $id) {
        ... on ProjectV2 {
          fields(first: 100) {
            nodes {
              __typename
              ... on ProjectV2FieldCommon {
                id
                name
                dataType
              }
            }
          }
        }
      }
    }
    """
    data = gh.graphql(query, {"id": project_id})
    if not gh.apply:
        return set()
    return {
        node["name"]
        for node in data["node"]["fields"]["nodes"]
        if node and node.get("name")
    }


def ensure_project_fields(
    gh: GitHub,
    project_id: str,
    issue_fields: dict[str, dict[str, Any]],
) -> None:
    names = project_field_names(gh, project_id)
    for name, field in issue_fields.items():
        if name in names:
            continue
        mutation = """
        mutation($projectId: ID!, $issueFieldId: ID!) {
          createProjectV2IssueField(input: {
            projectId: $projectId,
            issueFieldId: $issueFieldId
          }) {
            projectV2Field {
              __typename
              ... on ProjectV2FieldCommon {
                id
                name
                dataType
              }
            }
          }
        }
        """
        gh.graphql(
            mutation,
            {"projectId": project_id, "issueFieldId": field["node_id"]},
        )
        names.add(name)

    local_fields = [
        ("Target start", "DATE", None),
        ("Target completion", "DATE", None),
        (
            "Delivery confidence",
            "SINGLE_SELECT",
            [
                {"name": "High", "description": "Delivery is highly likely.", "color": "GREEN"},
                {"name": "Medium", "description": "Delivery has material uncertainty.", "color": "YELLOW"},
                {"name": "Low", "description": "Delivery is currently unlikely.", "color": "RED"},
            ],
        ),
    ]
    for name, data_type, options in local_fields:
        if name in names:
            continue
        mutation = """
        mutation(
          $projectId: ID!,
          $name: String!,
          $dataType: ProjectV2CustomFieldType!,
          $singleSelectOptions: [ProjectV2SingleSelectFieldOptionInput!]
        ) {
          createProjectV2Field(input: {
            projectId: $projectId,
            name: $name,
            dataType: $dataType,
            singleSelectOptions: $singleSelectOptions
          }) {
            projectV2Field {
              ... on ProjectV2Field { id name }
              ... on ProjectV2SingleSelectField { id name }
            }
          }
        }
        """
        gh.graphql(
            mutation,
            {
                "projectId": project_id,
                "name": name,
                "dataType": data_type,
                "singleSelectOptions": options,
            },
        )
        names.add(name)


def add_issues_to_project(
    gh: GitHub,
    project_id: str,
    issues: dict[str, dict[str, Any]],
) -> None:
    mutation = """
    mutation($projectId: ID!, $contentId: ID!) {
      addProjectV2ItemById(input: {
        projectId: $projectId,
        contentId: $contentId
      }) {
        item { id }
      }
    }
    """
    for wbs_id, issue in issues.items():
        try:
            gh.graphql(
                mutation,
                {"projectId": project_id, "contentId": issue["node_id"]},
            )
            print(f"PROJECT {wbs_id}")
        except GitHubError as exc:
            # GitHub returns an error if the item is already present.
            if "already exists" not in str(exc).lower():
                raise
            print(f"SKIP existing project item {wbs_id}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default="Summer-Studentship/Summer-Studentship")
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("github_software_issue_bodies_v0.1.json"),
    )
    parser.add_argument("--project-title", default="Studentship — Software WBS")
    parser.add_argument("--apply", action="store_true", help="Perform writes. Default is dry-run.")
    parser.add_argument("--delay", type=float, default=1.05)
    parser.add_argument(
        "--skip-organization-schema",
        action="store_true",
        help="Skip issue type and issue field creation if the token lacks org permissions.",
    )
    parser.add_argument("--skip-project", action="store_true")
    args = parser.parse_args()

    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        print("GITHUB_TOKEN is not set.", file=sys.stderr)
        return 2

    owner, repo = split_repo(args.repo)
    rows = load_manifest(args.manifest)
    validate_issue_field_definitions()
    gh = GitHub(token, apply=args.apply, delay=args.delay)

    repository = gh.rest("GET", f"/repos/{owner}/{repo}")
    if not repository.get("permissions", {}).get("push", False):
        raise GitHubError(
            f"The token does not have push/write access to {args.repo}. "
            "Install/authorize it for the organization repository first."
        )

    if args.skip_organization_schema:
        issue_types = {}
        issue_fields = {
            x["name"]: x for x in gh.rest("GET", f"/orgs/{owner}/issue-fields")
        }
    else:
        issue_types = ensure_issue_types(gh, owner)
        issue_fields = ensure_issue_fields(gh, owner)

    print(f"Issue types available: {len(issue_types)}")
    print(f"Issue fields available: {len(issue_fields)}")

    milestones = ensure_milestones(gh, owner, repo)
    issues = ensure_issues(gh, owner, repo, rows, issue_fields, milestones)

    if args.apply:
        ensure_hierarchy(gh, owner, repo, rows, issues)
        ensure_dependencies(gh, owner, repo, rows, issues)

    if not args.skip_project:
        print("RESUME Project setup: reuse/create Project, link fields, then add issue items.")
        project_id = graphql_owner_and_project(
            gh, owner, repository["node_id"], args.project_title
        )
        ensure_project_fields(gh, project_id, issue_fields)
        add_issues_to_project(gh, project_id, issues)

    output = {
        wbs_id: {
            "number": issue.get("number"),
            "id": issue.get("id"),
            "node_id": issue.get("node_id"),
            "html_url": issue.get("html_url"),
        }
        for wbs_id, issue in issues.items()
    }
    Path("github_software_issue_map.json").write_text(
        json.dumps(output, indent=2), encoding="utf-8"
    )

    mode = "APPLIED" if args.apply else "DRY-RUN COMPLETE"
    print(f"{mode}: {len(rows)} WBS records processed.")
    print(
        "Project views and Project automations are not currently exposed by the "
        "public Projects API; configure them from github_project_configuration_v0.1.md."
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except GitHubError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
