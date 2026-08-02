#!/usr/bin/env python3
"""CLI wrapper for Regional2D coupling to OpenFOAM boundaryData conversion."""

from openfoam_replay import command_convert


if __name__ == "__main__":
    raise SystemExit(command_convert(__import__("sys").argv[1:]))
