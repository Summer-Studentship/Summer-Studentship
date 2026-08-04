#!/usr/bin/env python3
"""Run the Kamaishi real-data hybrid delivery integration pipeline."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import kamaishi_delivery as delivery


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case-root", default="/tmp/kamaishi-hybrid-delivery/case")
    parser.add_argument("--output-root", default="/tmp/kamaishi-hybrid-delivery")
    parser.add_argument("--profile", default="etopo-500m", choices=["etopo-500m", "etopo-1000m"])
    parser.add_argument("--acquire", action="store_true", help="download missing source files before running")
    parser.add_argument("--offline", action="store_true", help="verify existing source files without network access")
    parser.add_argument("--overwrite", action="store_true", help="replace generated output roots")
    parser.add_argument("--python", default=str(delivery.DEFAULT_PYTHON), help="Python interpreter with rasterio, pyproj and clawpack")
    parser.add_argument("--r2d-binary", default=str(delivery.repo_root() / delivery.DEFAULT_R2D_BINARY))
    parser.add_argument("--skip-openfoam", action="store_true", help="assemble and run Regional2D/replay preparation only")
    args = parser.parse_args()

    try:
        evidence = delivery.run_delivery(
            case_root=Path(args.case_root),
            output_root=Path(args.output_root),
            profile_name=args.profile,
            acquire=args.acquire,
            offline=args.offline,
            overwrite=args.overwrite,
            python=Path(args.python),
            r2d_binary=Path(args.r2d_binary),
            run_openfoam=not args.skip_openfoam,
        )
    except delivery.DeliveryError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(f"case_root={evidence['build']['case_root']}")
    print(f"output_root={evidence['build']['output_root']}")
    print(f"summary={Path(args.output_root) / 'delivery_summary.json'}")
    print(delivery.tree(Path(args.output_root)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
