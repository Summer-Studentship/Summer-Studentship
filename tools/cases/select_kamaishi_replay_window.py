#!/usr/bin/env python3
"""Select a Kamaishi Local3D replay window from a G3 coupling export."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import kamaishi_delivery as delivery


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--coupling-dir", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--inward-normal-x", type=float, required=True)
    parser.add_argument("--inward-normal-y", type=float, required=True)
    args = parser.parse_args()
    evidence = delivery.select_replay_window(
        Path(args.coupling_dir),
        Path(args.output_dir),
        (args.inward_normal_x, args.inward_normal_y),
    )
    print(json.dumps(evidence, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
