#!/usr/bin/env python3
"""Build the deliverables project citation bank from Research bibliographies."""

from __future__ import annotations

import json
import re
import unicodedata
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RES_ROOT = ROOT / "docs/workstream/RES - Research"
OUT_DIR = ROOT / "deliverables/references"
OUT_BIB = OUT_DIR / "project-citation-bank.bib"
OUT_REGISTER = OUT_DIR / "project-citation-register.json"
OUT_CONFLICTS = OUT_DIR / "project-citation-conflicts.json"

REVIEWED_SUBJECTS = [
    {
        "subject": "TsuPy / tsunami uncertainty sensitivity",
        "status": "pending_review",
        "role": "uncertainty and sensitivity context",
        "keywords": ["tsupy", "uncertainty", "sensitivity"],
    },
    {
        "subject": "Tohoku finite-fault source sensitivity",
        "status": "approved_with_conditions",
        "role": "source uncertainty and source-model sensitivity",
        "keywords": ["finite", "fault", "source", "tohoku"],
    },
    {
        "subject": "2011 Tohoku dispersive source inversion",
        "status": "approved_with_conditions",
        "role": "dispersion and model-form limitation",
        "keywords": ["dispersion", "tohoku"],
    },
    {
        "subject": "Tohoku waveform inversion with source kinematics",
        "status": "approved_with_conditions",
        "role": "source kinematics and waveform inversion",
        "keywords": ["waveform", "inversion", "source"],
    },
    {
        "subject": "Sendai/Tohoku Manning and nested-grid modelling",
        "status": "approved_with_conditions",
        "role": "Manning friction and nested-grid methodology",
        "keywords": ["manning", "nested", "japan", "tsunami"],
    },
    {
        "subject": "high-resolution Tohoku bathymetry/inundation modelling",
        "status": "approved_with_conditions",
        "role": "bathymetric fidelity and inundation modelling",
        "keywords": ["bathymetry", "inundation", "tohoku"],
    },
    {
        "subject": "urban-grid-resolution sensitivity",
        "status": "approved_with_conditions",
        "role": "grid-resolution sensitivity",
        "keywords": ["grid", "resolution", "inundation"],
    },
    {
        "subject": "buoyancy-modified k-omega SST / OpenFOAM free-surface turbulence",
        "status": "approved_with_conditions",
        "role": "Local3D turbulence-model sensitivity",
        "keywords": ["turbulence", "sst", "openfoam"],
    },
    {
        "subject": "Kamaishi breakwater overtopping and turbulence-model sensitivity",
        "status": "approved_with_conditions",
        "role": "Kamaishi overtopping and turbulence sensitivity",
        "keywords": ["kamaishi", "breakwater", "turbulence"],
    },
    {
        "subject": "porous-breakwater OpenFOAM calibration",
        "status": "approved_future_work",
        "role": "future porous/rubble-mound calibration only",
        "keywords": ["porous", "breakwater", "openfoam"],
    },
]


@dataclass
class BibEntry:
    entry_type: str
    key: str
    fields: dict[str, str]
    raw: str
    source: Path


def normalise_text(value: str) -> str:
    value = value.replace("\\&", " and ")
    value = re.sub(r"\\[a-zA-Z]+\*?(?:\[[^\]]*\])?(?:\{([^{}]*)\})?", r"\1", value)
    value = value.replace("{", "").replace("}", "")
    value = unicodedata.normalize("NFKD", value)
    value = "".join(ch for ch in value if not unicodedata.combining(ch))
    value = value.casefold()
    return re.sub(r"[^a-z0-9]+", " ", value).strip()


def normalise_doi(value: str) -> str:
    value = value.strip().strip("{}").strip()
    value = re.sub(r"^https?://(?:dx\.)?doi\.org/", "", value, flags=re.I)
    value = re.sub(r"^doi:", "", value, flags=re.I)
    return value.casefold().strip()


def normalise_url(value: str) -> str:
    value = value.strip().strip("{}").strip()
    value = re.sub(r"#.*$", "", value)
    value = re.sub(r"/+$", "", value)
    return value.casefold()


def first_author(fields: dict[str, str]) -> str:
    author = fields.get("author") or fields.get("organization") or ""
    first = author.split(" and ", 1)[0]
    return normalise_text(first)


def identity(entry: BibEntry) -> tuple[str, str]:
    doi = normalise_doi(entry.fields.get("doi", ""))
    if doi:
        return ("doi", doi)
    url = normalise_url(entry.fields.get("url", ""))
    if url:
        return ("url", url)
    title = normalise_text(entry.fields.get("title", ""))
    year = normalise_text(entry.fields.get("year", ""))
    return ("fallback", f"{title}|{year}|{first_author(entry.fields)}")


def parse_entries(path: Path) -> list[BibEntry]:
    text = path.read_text(encoding="utf-8")
    entries: list[BibEntry] = []
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
            char = text[cursor]
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    end = cursor + 1
                    break
        if end is None:
            raise SystemExit(f"Unclosed BibTeX entry in {path} at byte {start}")
        raw = text[start:end].strip()
        entry_type = match.group(1).lower()
        key = match.group(2).strip()
        body = raw[raw.find(",", raw.find("{")) + 1:-1]
        fields = parse_fields(body)
        entries.append(BibEntry(entry_type=entry_type, key=key, fields=fields, raw=raw, source=path))
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


def field_conflicts(left: BibEntry, right: BibEntry) -> list[str]:
    conflicts = []
    for field in ("title", "year", "doi"):
        a = normalise_doi(left.fields.get(field, "")) if field == "doi" else normalise_text(left.fields.get(field, ""))
        b = normalise_doi(right.fields.get(field, "")) if field == "doi" else normalise_text(right.fields.get(field, ""))
        if a and b and a != b:
            conflicts.append(field)
    return conflicts


def find_pdf_paths(entry: BibEntry, pdfs: list[Path]) -> list[str]:
    title = normalise_text(entry.fields.get("title", ""))
    if not title:
        return []
    title_tokens = set(title.split())
    matches = []
    for pdf in pdfs:
        name = normalise_text(pdf.stem)
        name_tokens = set(name.split())
        if title and (title in name or name in title):
            matches.append(str(pdf.relative_to(ROOT)))
            continue
        if title_tokens:
            overlap = len(title_tokens & name_tokens) / max(len(title_tokens), 1)
            if overlap >= 0.65:
                matches.append(str(pdf.relative_to(ROOT)))
    return sorted(set(matches))


def role_for(entry: BibEntry) -> str:
    source = str(entry.source)
    title = normalise_text(entry.fields.get("title", ""))
    if "res-dat" in source:
        return "case-study data, source, terrain or observational evidence"
    if "res-mod" in source:
        return "mathematical-model authority"
    if "res-num" in source:
        return "numerical-method authority"
    if "res-phy" in source:
        return "tsunami physics and source-process authority"
    if "res-ver" in source:
        return "verification and validation methodology"
    if "res-geo" in source:
        return "barrier geometry and representation context"
    if "porous" in title:
        return "future porous/rubble-mound modelling"
    return "supporting research context"


def usage_flags(entry: BibEntry) -> dict[str, object]:
    title = normalise_text(entry.fields.get("title", ""))
    role = role_for(entry)
    future = "future" in role or "porous" in title or "fluid structure" in title
    return {
        "review_status": "approved_future_work" if future else "approved",
        "project_role": role,
        "report_use": True,
        "poster_use": False,
        "calibration_use": "future_only" if future else ("methodology" if any(word in title for word in ("manning", "sensitivity", "source", "bathymetry", "turbulence")) else False),
        "validation_use": "methodology" if "validation" in title or "observation" in title or "survey" in title else False,
        "future_work": future,
    }


def escape_bib_value(value: str) -> str:
    return value.replace("\n", " ")


def output_entry_type(entry: BibEntry) -> str:
    if entry.entry_type != "online" and entry.fields.get("url") and not entry.fields.get("year"):
        return "online"
    return entry.entry_type


def format_entry(entry: BibEntry) -> str:
    field_order = ["author", "organization", "title", "journal", "year", "volume", "number", "pages", "doi", "url", "urldate", "note"]
    lines = [f"@{output_entry_type(entry)}{{{entry.key},"]
    used = set()
    for field in field_order:
        if field in entry.fields:
            used.add(field)
            lines.append(f"  {field:<12}= {{{escape_bib_value(entry.fields[field])}}},")
    for field in sorted(set(entry.fields) - used):
        lines.append(f"  {field:<12}= {{{escape_bib_value(entry.fields[field])}}},")
    if lines[-1].endswith(","):
        lines[-1] = lines[-1][:-1]
    lines.append("}")
    return "\n".join(lines)


def main() -> int:
    bib_paths = sorted(path for path in RES_ROOT.rglob("res-*.bib") if path.resolve() != OUT_BIB.resolve())
    pdf_paths = sorted(RES_ROOT.rglob("*.pdf"))
    entries = [entry for path in bib_paths for entry in parse_entries(path)]
    by_key: dict[str, BibEntry] = {}
    by_identity: dict[tuple[str, str], BibEntry] = {}
    sources: dict[str, set[str]] = {}
    aliases: dict[str, set[str]] = {}
    duplicate_records = []
    conflicts = []

    for entry in entries:
        ident = identity(entry)
        if entry.key in by_key and identity(by_key[entry.key]) != ident:
            conflicts.append({"type": "citation-key", "key": entry.key, "sources": [str(by_key[entry.key].source), str(entry.source)]})
            continue
        by_key.setdefault(entry.key, entry)
        if ident in by_identity:
            canonical = by_identity[ident]
            conflict_fields = field_conflicts(canonical, entry)
            if conflict_fields:
                conflicts.append({"type": "metadata", "key": entry.key, "canonical_key": canonical.key, "fields": conflict_fields})
                continue
            sources.setdefault(canonical.key, set()).add(str(entry.source.relative_to(ROOT)))
            aliases.setdefault(canonical.key, set()).add(entry.key)
            duplicate_records.append({"canonical_key": canonical.key, "alias_key": entry.key, "identity": list(ident)})
            continue
        by_identity[ident] = entry
        sources.setdefault(entry.key, set()).add(str(entry.source.relative_to(ROOT)))
        aliases.setdefault(entry.key, set()).add(entry.key)

    if conflicts:
        OUT_DIR.mkdir(parents=True, exist_ok=True)
        OUT_CONFLICTS.write_text(json.dumps(conflicts, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"citation conflicts detected: {len(conflicts)}")
        return 1
    if OUT_CONFLICTS.exists():
        OUT_CONFLICTS.unlink()

    unique = sorted(by_identity.values(), key=lambda item: item.key.casefold())
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    bib_header = [
        "% Project citation bank",
        "% Generated from docs/workstream/RES - Research/**/res-*.bib.",
        "% Research-domain bibliography files remain the source evidence records.",
        "",
    ]
    OUT_BIB.write_text("\n\n".join(bib_header + [format_entry(entry) for entry in unique]) + "\n", encoding="utf-8")

    records = []
    for entry in unique:
        flags = usage_flags(entry)
        records.append({
            "citation_key": entry.key,
            "title": entry.fields.get("title", ""),
            "authors": entry.fields.get("author") or entry.fields.get("organization") or "",
            "year": entry.fields.get("year", ""),
            "doi": normalise_doi(entry.fields.get("doi", "")),
            "entry_type": output_entry_type(entry),
            "source_bibliographies": sorted(sources.get(entry.key, [])),
            "repository_pdf_paths": find_pdf_paths(entry, pdf_paths),
            "aliases": sorted(aliases.get(entry.key, {entry.key}) - {entry.key}),
            "review_status": flags["review_status"],
            "project_role": flags["project_role"],
            "report_use": flags["report_use"],
            "poster_use": flags["poster_use"],
            "calibration_use": flags["calibration_use"],
            "validation_use": flags["validation_use"],
            "future_work": flags["future_work"],
            "notes": "",
        })

    reviewed = []
    pdf_text = "\n".join(str(path.relative_to(ROOT)).casefold() for path in pdf_paths)
    title_text = "\n".join(normalise_text(entry.fields.get("title", "")) for entry in unique)
    for subject in REVIEWED_SUBJECTS:
        keyword_hits = [keyword for keyword in subject["keywords"] if keyword in pdf_text or keyword in title_text]
        reviewed.append({
            "subject": subject["subject"],
            "review_status": subject["status"],
            "project_role": subject["role"],
            "matched_keywords": keyword_hits,
            "exact_research_pdf_paths": [],
            "related_research_pdf_paths": [
                str(path.relative_to(ROOT)) for path in pdf_paths
                if any(keyword in normalise_text(path.stem) for keyword in subject["keywords"])
            ],
            "local_pdf_status": "related_only_or_absent" if keyword_hits else "no_exact_local_pdf_identified",
            "notes": "Exact reviewed-paper metadata was not fabricated; related Research records are distinguished from exact local PDFs.",
        })

    payload = {
        "schema": {"name": "tsunami.project_citation_register", "version": "1.0.0"},
        "generated_from": [str(path.relative_to(ROOT)) for path in bib_paths],
        "audit": {
            "research_bibliography_count": len(bib_paths),
            "raw_entry_count": len(entries),
            "unique_citation_count": len(unique),
            "unique_doi_count": len({normalise_doi(entry.fields.get("doi", "")) for entry in unique if normalise_doi(entry.fields.get("doi", ""))}),
            "duplicate_entries_resolved": len(duplicate_records),
            "citation_key_conflicts": 0,
            "metadata_conflicts": 0,
            "duplicate_records": duplicate_records,
        },
        "reviewed_literature_audit": reviewed,
        "records": records,
    }
    OUT_REGISTER.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"research bibliographies: {len(bib_paths)}")
    print(f"raw entries: {len(entries)}")
    print(f"unique citations: {len(unique)}")
    print(f"duplicates resolved: {len(duplicate_records)}")
    print("conflicts: 0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
