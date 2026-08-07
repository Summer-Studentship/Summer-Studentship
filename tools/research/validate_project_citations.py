#!/usr/bin/env python3
"""Validate the deliverables project citation bank and register."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BANK = ROOT / "deliverables/references/project-citation-bank.bib"
REGISTER = ROOT / "deliverables/references/project-citation-register.json"
REPORT_ROOT = ROOT / "deliverables/report"


def normalise_doi(value: str) -> str:
    value = value.strip()
    value = re.sub(r"^https?://(?:dx\.)?doi\.org/", "", value, flags=re.I)
    value = re.sub(r"^doi:", "", value, flags=re.I)
    return value.casefold().strip()


def parse_entries(path: Path) -> dict[str, dict[str, str]]:
    text = path.read_text(encoding="utf-8")
    entries: dict[str, dict[str, str]] = {}
    index = 0
    while True:
        start = text.find("@", index)
        if start < 0:
            break
        match = re.match(r"@([A-Za-z]+)\s*\{\s*([^,\s]+)\s*,", text[start:])
        if not match:
            index = start + 1
            continue
        depth = 0
        end = None
        for cursor in range(start, len(text)):
            if text[cursor] == "{":
                depth += 1
            elif text[cursor] == "}":
                depth -= 1
                if depth == 0:
                    end = cursor + 1
                    break
        if end is None:
            raise ValueError(f"unclosed BibTeX entry at byte {start}")
        if match.group(2) in entries:
            entries.setdefault("__duplicate_keys__", {})[match.group(2)] = "duplicate"
        raw = text[start:end]
        body = raw[raw.find(",", raw.find("{")) + 1:-1]
        fields = {"entry_type": match.group(1).lower(), **parse_fields(body)}
        entries[match.group(2)] = fields
        index = end
    return entries


def parse_fields(body: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    index = 0
    while index < len(body):
        while index < len(body) and body[index] in ", \t\r\n":
            index += 1
        name_match = re.match(r"[A-Za-z][A-Za-z0-9_-]*", body[index:])
        if not name_match:
            break
        field = name_match.group(0).lower()
        index += len(name_match.group(0))
        while index < len(body) and body[index].isspace():
            index += 1
        if index >= len(body) or body[index] != "=":
            break
        index += 1
        while index < len(body) and body[index].isspace():
            index += 1
        if index >= len(body):
            fields[field] = ""
            break

        if body[index] == "{":
            start = index + 1
            depth = 1
            index += 1
            while index < len(body) and depth:
                if body[index] == "\\":
                    index += 2
                    continue
                if body[index] == "{":
                    depth += 1
                elif body[index] == "}":
                    depth -= 1
                index += 1
            value = body[start:index - 1] if depth == 0 else body[start:index]
        elif body[index] == '"':
            start = index + 1
            index += 1
            while index < len(body):
                if body[index] == "\\":
                    index += 2
                    continue
                if body[index] == '"':
                    break
                index += 1
            value = body[start:index]
            index += 1
        else:
            start = index
            while index < len(body) and body[index] not in ",\n":
                index += 1
            value = body[start:index]
        fields[field] = re.sub(r"\s+", " ", value).strip()
    return fields


def report_citations() -> set[str]:
    keys: set[str] = set()
    for path in [REPORT_ROOT / "report.tex", *sorted((REPORT_ROOT / "sections").glob("*.tex"))]:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        for match in re.finditer(r"\\(?:auto|text|super)?cite\{([^{}]+)\}", text):
            keys.update(key.strip() for key in match.group(1).split(",") if key.strip())
    return keys


def main() -> int:
    errors: list[str] = []
    entries = parse_entries(BANK)
    duplicate_keys = entries.pop("__duplicate_keys__", {})
    if duplicate_keys:
        errors.append(f"duplicate citation keys: {sorted(duplicate_keys)}")
    dois: dict[str, str] = {}
    for key, fields in entries.items():
        if not fields.get("title"):
            errors.append(f"{key}: missing title")
        if not (fields.get("author") or fields.get("organization")):
            errors.append(f"{key}: missing author/organisation")
        if fields["entry_type"] not in {"online"} and not fields.get("year"):
            errors.append(f"{key}: missing year")
        doi = normalise_doi(fields.get("doi", ""))
        if doi:
            if not re.match(r"^10\.\S+/.+", doi):
                errors.append(f"{key}: invalid DOI format {doi}")
            if doi in dois and dois[doi] != key:
                errors.append(f"duplicate DOI {doi}: {dois[doi]} and {key}")
            dois[doi] = key

    register = json.loads(REGISTER.read_text(encoding="utf-8"))
    registered = {record["citation_key"]: record for record in register.get("records", [])}
    for key in entries:
        if key not in registered:
            errors.append(f"{key}: bank citation absent from register")
    for key, record in registered.items():
        if key not in entries:
            errors.append(f"{key}: register record absent from bank")
        status = record.get("review_status")
        if status not in {"approved", "approved_with_conditions", "approved_future_work", "research_context", "pending_review", "rejected"}:
            errors.append(f"{key}: invalid review_status {status}")
        for rel_path in record.get("repository_pdf_paths", []):
            if not (ROOT / rel_path).is_file():
                errors.append(f"{key}: invalid repository PDF path {rel_path}")

    missing = sorted(report_citations() - set(entries))
    if missing:
        errors.append(f"report citations absent from bank: {missing}")

    print(f"bank citations: {len(entries)}")
    print(f"register records: {len(registered)}")
    print(f"report citations missing from bank: {len(missing)}")
    print(f"invalid repository PDF references: {sum('invalid repository PDF path' in error for error in errors)}")
    if errors:
        for error in errors:
            print(f"validation error: {error}", file=sys.stderr)
        return 1
    print("project citation validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
