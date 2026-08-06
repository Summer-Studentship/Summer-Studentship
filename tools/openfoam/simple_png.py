#!/usr/bin/env python3
"""Tiny dependency-free PNG writer for single-panel evidence plots."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path
from typing import Iterable, Sequence


Color = tuple[int, int, int]


WHITE: Color = (255, 255, 255)
BLACK: Color = (25, 25, 25)
GRID: Color = (220, 225, 230)
AXIS: Color = (70, 75, 80)


def _chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def _set_pixel(image: list[bytearray], x: int, y: int, color: Color) -> None:
    if y < 0 or y >= len(image) or x < 0 or x >= (len(image[0]) // 3):
        return
    offset = x * 3
    image[y][offset:offset + 3] = bytes(color)


def _line(image: list[bytearray], x0: int, y0: int, x1: int, y1: int, color: Color) -> None:
    dx = abs(x1 - x0)
    sx = 1 if x0 < x1 else -1
    dy = -abs(y1 - y0)
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        _set_pixel(image, x0, y0, color)
        if x0 == x1 and y0 == y1:
            break
        twice = 2 * err
        if twice >= dy:
            err += dy
            x0 += sx
        if twice <= dx:
            err += dx
            y0 += sy


def _rect(image: list[bytearray], x0: int, y0: int, x1: int, y1: int, color: Color) -> None:
    left, right = sorted((max(0, x0), min(len(image[0]) // 3 - 1, x1)))
    top, bottom = sorted((max(0, y0), min(len(image) - 1, y1)))
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            _set_pixel(image, x, y, color)


def write_line_plot_png(
    path: Path,
    series: Sequence[dict],
    *,
    windows: Sequence[dict] = (),
    metadata: dict[str, str] | None = None,
    width: int = 900,
    height: int = 520,
) -> None:
    """Write a single-panel RGB PNG line plot from numeric series.

    The image intentionally keeps visual requirements small: axes, grid,
    shaded windows and line traces. Labels and numeric annotations are stored
    in PNG text metadata so the plot stays deterministic without a font stack.
    """
    if not series:
        raise ValueError("at least one series is required")
    xs = [float(x) for item in series for x in item["x"]]
    ys = [float(y) for item in series for y in item["y"]]
    if not xs or not ys:
        raise ValueError("series must contain data")
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    if xmax <= xmin:
        xmax = xmin + 1.0
    if ymax <= ymin:
        pad = max(abs(ymax), 1.0)
        ymin -= pad
        ymax += pad
    ypad = 0.08 * (ymax - ymin)
    ymin -= ypad
    ymax += ypad

    left, right, top, bottom = 72, width - 34, 34, height - 64
    image = [bytearray(WHITE * width) for _ in range(height)]

    def sx(value: float) -> int:
        return int(round(left + (value - xmin) * (right - left) / (xmax - xmin)))

    def sy(value: float) -> int:
        return int(round(bottom - (value - ymin) * (bottom - top) / (ymax - ymin)))

    for i in range(6):
        x = left + i * (right - left) // 5
        _line(image, x, top, x, bottom, GRID)
    for i in range(5):
        y = top + i * (bottom - top) // 4
        _line(image, left, y, right, y, GRID)
    for window in windows:
        color = tuple(int(c) for c in window.get("color", (235, 242, 255)))
        _rect(image, sx(float(window["start"])), top, sx(float(window["end"])), bottom, color)  # type: ignore[arg-type]
    _line(image, left, bottom, right, bottom, AXIS)
    _line(image, left, top, left, bottom, AXIS)
    zero = sy(0.0)
    if top <= zero <= bottom:
        _line(image, left, zero, right, zero, (165, 170, 175))

    for item in series:
        x_values = [float(x) for x in item["x"]]
        y_values = [float(y) for y in item["y"]]
        color = tuple(int(c) for c in item.get("color", BLACK))
        for x0, y0, x1, y1 in zip(x_values, y_values, x_values[1:], y_values[1:]):
            _line(image, sx(x0), sy(y0), sx(x1), sy(y1), color)  # type: ignore[arg-type]

    raw = b"".join(b"\x00" + bytes(row) for row in image)
    payload = [
        b"\x89PNG\r\n\x1a\n",
        _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)),
    ]
    for key, value in (metadata or {}).items():
        payload.append(_chunk(b"tEXt", key.encode("latin-1", "replace") + b"\x00" + value.encode("latin-1", "replace")))
    payload.extend([
        _chunk(b"IDAT", zlib.compress(raw, 9)),
        _chunk(b"IEND", b""),
    ])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"".join(payload))
