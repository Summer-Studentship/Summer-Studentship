#!/usr/bin/env python3
"""Frame manifest, FFmpeg assembly and ffprobe validation helpers for R14."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Sequence


def utc_now() -> str:
    return datetime.now(UTC).isoformat().replace("+00:00", "Z")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def command_output(command: Sequence[str]) -> tuple[int, str]:
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    return completed.returncode, completed.stdout.strip()


def manifest_frames(frame_dir: Path, output: Path, *, data_class: str, source: str) -> dict[str, Any]:
    frames = sorted(frame_dir.glob("*.png"))
    payload = {
        "schema": {"name": "tsunami.r14.frame_manifest", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "frame_dir": frame_dir.as_posix(),
        "data_class": data_class,
        "source": source,
        "frame_count": len(frames),
        "frames": [{"path": path.as_posix(), "sha256": sha256(path)} for path in frames],
    }
    write_json(output, payload)
    return payload


def assemble_mp4(frame_pattern: str, output: Path, *, fps: int) -> dict[str, Any]:
    command = [
        "ffmpeg",
        "-y",
        "-framerate",
        str(fps),
        "-i",
        frame_pattern,
        "-vf",
        "format=yuv420p",
        "-movflags",
        "+faststart",
        output.as_posix(),
    ]
    code, text = command_output(command)
    payload = {
        "schema": {"name": "tsunami.r14.ffmpeg_assembly", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "command": command,
        "returncode": code,
        "output": output.as_posix(),
        "sha256": sha256(output) if output.is_file() else None,
        "log_tail": "\n".join(text.splitlines()[-20:]),
    }
    return payload


def probe_mp4(path: Path) -> dict[str, Any]:
    command = [
        "ffprobe",
        "-v",
        "error",
        "-select_streams",
        "v:0",
        "-show_entries",
        "stream=width,height,r_frame_rate,duration,nb_frames",
        "-show_entries",
        "format=duration",
        "-of",
        "json",
        path.as_posix(),
    ]
    code, text = command_output(command)
    raw = json.loads(text) if code == 0 and text else {}
    stream = (raw.get("streams") or [{}])[0]
    fmt = raw.get("format") or {}
    return {
        "schema": {"name": "tsunami.r14.ffprobe_validation", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "status": "PASSED" if code == 0 else "FAILED",
        "path": path.as_posix(),
        "sha256": sha256(path) if path.is_file() else None,
        "duration_s": float(stream.get("duration") or fmt.get("duration") or 0.0),
        "resolution": [int(stream.get("width", 0)), int(stream.get("height", 0))],
        "fps": stream.get("r_frame_rate"),
        "frame_count": int(stream["nb_frames"]) if str(stream.get("nb_frames", "")).isdigit() else None,
        "returncode": code,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    manifest = sub.add_parser("manifest-frames")
    manifest.add_argument("--frame-dir", required=True, type=Path)
    manifest.add_argument("--output", required=True, type=Path)
    manifest.add_argument("--data-class", required=True)
    manifest.add_argument("--source", required=True)
    assemble = sub.add_parser("assemble-mp4")
    assemble.add_argument("--frame-pattern", required=True)
    assemble.add_argument("--output", required=True, type=Path)
    assemble.add_argument("--fps", type=int, default=2)
    probe = sub.add_parser("probe-mp4")
    probe.add_argument("--path", required=True, type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.command == "manifest-frames":
        payload = manifest_frames(args.frame_dir, args.output, data_class=args.data_class, source=args.source)
    elif args.command == "assemble-mp4":
        payload = assemble_mp4(args.frame_pattern, args.output, fps=args.fps)
    else:
        payload = probe_mp4(args.path)
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if payload.get("returncode", 0) == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
