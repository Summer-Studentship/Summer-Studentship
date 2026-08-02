#!/usr/bin/env python3
"""CLI wrapper for deterministic synthetic OpenFOAM replay case generation."""

from openfoam_replay import command_generate


if __name__ == "__main__":
    raise SystemExit(command_generate(__import__("sys").argv[1:]))
