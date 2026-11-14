#!/usr/bin/env python3
"""Verify the native VST3 and Standalone outputs before CI packages them."""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"Native package verification failed: {message}")


def require_one(paths: list[Path], description: str) -> Path:
    if len(paths) != 1:
        fail(f"expected exactly one {description}, found {len(paths)}: {paths}")
    return paths[0]


def verify_elf_x86_64(binary: Path) -> None:
    data = binary.read_bytes()[:20]
    if len(data) < 20 or data[:4] != b"\x7fELF":
        fail(f"{binary} is not an ELF executable")
    byte_order = "<" if data[5] == 1 else ">" if data[5] == 2 else None
    if byte_order is None:
        fail(f"{binary} has an invalid ELF byte order")
    machine = struct.unpack(f"{byte_order}H", data[18:20])[0]
    if machine != 62:
        fail(f"{binary} is ELF machine {machine}; expected x86-64 (62)")


def verify_pe_x86_64(binary: Path) -> None:
    data = binary.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        fail(f"{binary} is not a PE executable")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        fail(f"{binary} has no valid PE signature")
    machine = struct.unpack_from("<H", data, pe_offset + 4)[0]
    if machine != 0x8664:
        fail(f"{binary} is PE machine 0x{machine:04x}; expected x86-64 (0x8664)")


def verify_macos_universal(binary: Path) -> None:
    result = subprocess.run(
        ["lipo", "-archs", str(binary)],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        fail(f"could not inspect macOS architectures for {binary}: {result.stderr.strip()}")
    architectures = set(result.stdout.split())
    missing = {"x86_64", "arm64"} - architectures
    if missing:
        fail(f"{binary} is missing universal architectures: {', '.join(sorted(missing))}")


def verify_architecture(binary: Path, platform: str) -> None:
    if not binary.is_file():
        fail(f"native binary does not exist: {binary}")
    if platform == "linux-x86_64":
        verify_elf_x86_64(binary)
    elif platform == "windows-x86_64":
        verify_pe_x86_64(binary)
    elif platform == "macos-universal":
        verify_macos_universal(binary)
    else:
        fail(f"unsupported platform: {platform}")


def locate_plugin_binary(bundle: Path, platform: str) -> Path:
    contents = bundle / "Contents"
    if platform == "linux-x86_64":
        candidates = list((contents / "x86_64-linux").glob("*.so"))
    elif platform == "windows-x86_64":
        candidates = [
            path for path in (contents / "x86_64-win").iterdir()
            if path.is_file()
        ] if (contents / "x86_64-win").is_dir() else []
    else:
        macos_dir = contents / "MacOS"
        candidates = [path for path in macos_dir.iterdir() if path.is_file()] if macos_dir.is_dir() else []
    return require_one(candidates, f"{platform} VST3 binary")


def locate_standalone_binary(standalone_dir: Path, platform: str) -> Path:
    if platform == "linux-x86_64":
        candidates = [
            path for path in standalone_dir.rglob("*")
            if path.is_file() and os.access(path, os.X_OK)
        ]
    elif platform == "windows-x86_64":
        candidates = list(standalone_dir.rglob("*.exe"))
    else:
        candidates = [
            path for path in standalone_dir.glob("*.app/Contents/MacOS/*")
            if path.is_file()
        ]
    return require_one(candidates, f"{platform} Standalone executable")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--platform",
        required=True,
        choices=("linux-x86_64", "windows-x86_64", "macos-universal"),
    )
    parser.add_argument("--root", type=Path, required=True, help="JUCE Release artefacts directory")
    args = parser.parse_args()

    if not args.root.is_dir():
        fail(f"build output directory does not exist: {args.root}")

    vst3_dir = args.root / "VST3"
    standalone_dir = args.root / "Standalone"
    if not vst3_dir.is_dir():
        fail(f"VST3 output directory is missing: {vst3_dir}")
    if not standalone_dir.is_dir():
        fail(f"Standalone output directory is missing: {standalone_dir}")

    bundles = [path for path in vst3_dir.rglob("*.vst3") if path.is_dir()]
    bundle = require_one(bundles, "VST3 bundle")
    if not (bundle / "Contents").is_dir():
        fail(f"VST3 bundle has no Contents directory: {bundle}")

    plugin_binary = locate_plugin_binary(bundle, args.platform)
    standalone_binary = locate_standalone_binary(standalone_dir, args.platform)
    verify_architecture(plugin_binary, args.platform)
    verify_architecture(standalone_binary, args.platform)

    print(
        "PASS: VST3 bundle and Standalone executable exist and match "
        f"{args.platform}: {plugin_binary}, {standalone_binary}"
    )


if __name__ == "__main__":
    main()
