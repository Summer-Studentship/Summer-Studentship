#!/usr/bin/env python3
"""CLI wrapper for the synthetic OpenFOAM replay smoke workflow."""

from openfoam_replay import command_smoke


if __name__ == "__main__":
    raise SystemExit(command_smoke(__import__("sys").argv[1:]))
