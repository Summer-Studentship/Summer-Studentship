#!/usr/bin/env python3
"""Run the Kamaishi real-data hybrid delivery integration pipeline."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import kamaishi_delivery as delivery


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case-root")
    parser.add_argument("--output-root")
    parser.add_argument("--artifact-root", help="persistent artifact root; defaults to the G6 Kamaishi root in acceptance mode")
    parser.add_argument("--profile", choices=["etopo-500m", "etopo-1000m"])
    parser.add_argument("--acquire", action="store_true", help="download missing source files before running")
    parser.add_argument("--offline", action="store_true", help="verify existing source files without network access")
    parser.add_argument("--overwrite", action="store_true", help="replace generated output roots")
    parser.add_argument("--python", default=str(delivery.DEFAULT_PYTHON), help="Python interpreter with rasterio, pyproj and clawpack")
    parser.add_argument("--r2d-binary", default=str(delivery.repo_root() / delivery.DEFAULT_R2D_BINARY))
    parser.add_argument("--skip-openfoam", action="store_true", help="assemble and run Regional2D/replay preparation only")
    parser.add_argument("--reuse-preprocessing", action="store_true", help="reuse completed preprocessing stages when checksums match")
    parser.add_argument("--g6-local3d-acceptance", action="store_true", help="run only the targeted G6 Local3D acceptance evidence path")
    parser.add_argument("--regional-evidence-end-time", type=float, help="Regional2D evidence end time for G6 Local3D acceptance")
    parser.add_argument("--fixed-replay-source-start", type=float, help="accepted fixed replay source start time")
    parser.add_argument("--fixed-replay-source-end", type=float, help="accepted fixed replay source end time")
    args = parser.parse_args()
    artifact_root = Path(args.artifact_root) if args.artifact_root else None
    if args.g6_local3d_acceptance and artifact_root is None and args.case_root is None and args.output_root is None:
        artifact_root = delivery.DEFAULT_G6_ARTIFACT_ROOT
    case_root = Path(args.case_root) if args.case_root else (artifact_root / "case" if artifact_root else Path("/tmp/kamaishi-hybrid-delivery/case"))
    output_root = Path(args.output_root) if args.output_root else (artifact_root if artifact_root else Path("/tmp/kamaishi-hybrid-delivery"))
    profile = args.profile or ("etopo-1000m" if args.g6_local3d_acceptance else "etopo-500m")
    reference = None
    if args.g6_local3d_acceptance:
        reference = delivery.load_g5_accepted_replay_reference()
        if args.fixed_replay_source_start is not None:
            reference["source_window"]["start_s"] = float(args.fixed_replay_source_start)
        if args.fixed_replay_source_end is not None:
            reference["source_window"]["end_s"] = float(args.fixed_replay_source_end)

    try:
        evidence = delivery.run_delivery(
            case_root=case_root,
            output_root=output_root,
            profile_name=profile,
            acquire=args.acquire,
            offline=args.offline,
            overwrite=args.overwrite,
            python=Path(args.python),
            r2d_binary=Path(args.r2d_binary),
            run_openfoam=not args.skip_openfoam,
            evidence_mode="g6_local3d_acceptance" if args.g6_local3d_acceptance else None,
            regional_evidence_end_time_s=args.regional_evidence_end_time,
            fixed_replay_reference=reference,
            reuse_preprocessing=args.reuse_preprocessing or args.g6_local3d_acceptance,
        )
    except delivery.DeliveryError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(f"case_root={evidence['build']['case_root']}")
    print(f"output_root={evidence['build']['output_root']}")
    print(f"summary={output_root / 'delivery_summary.json'}")
    print(delivery.tree(output_root))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
