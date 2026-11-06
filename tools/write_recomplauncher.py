#!/usr/bin/env python3
"""Write the release's RecompLauncher manifest; no game data or settings are shipped."""

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def profile(platform, appimage_arch=None):
    if platform not in ("windows", "linux", "macos"):
        raise ValueError("unsupported desktop platform")
    if appimage_arch is not None and (platform != "linux" or appimage_arch not in ("x86_64", "aarch64")):
        raise ValueError("AppImages need Linux and x86_64 or aarch64")

    result = json.loads((ROOT / "recomplauncher.json").read_text(encoding="utf-8"))
    legacy = {
        "windows": "{config}/Metroid Prime 2 Echoes",
        "linux": "{local}/Metroid Prime 2: Echoes",
        "macos": "{home}/Library/Application Support/Metroid Prime 2: Echoes",
    }[platform]
    if appimage_arch:
        # PortPaths keeps AppImage data beside the image, not under user/.
        programs = [f"MetroidPrime2-{appimage_arch}.AppImage"]
        roots = ["{install}", legacy]
        fallback = "{install}"
    elif platform == "macos":
        # Never write into the signed bundle, including its Resources directory.
        programs = ["Metroid Prime 2 Port.app/Contents/MacOS/metroid_prime2_port"]
        roots = [legacy]
        fallback = legacy
    else:
        programs = ["metroid_prime2_port.exe" if platform == "windows" else "metroid_prime2_port"]
        # Match DesktopUserFolder: portable settings win over legacy data;
        # an existing per-user install stays there until the player moves it.
        roots = ["{install}/user", legacy]
        fallback = "{install}/user"

    result["start"]["program"] = programs
    result["config"][0]["file"] = [
        {"path": f"{root}/settings.cfg", "if": f"{root}/settings.cfg"} for root in roots
    ] + [{"path": f"{fallback}/settings.cfg"}]
    folders = [
        {"path": root, "if": f"{root}/settings.cfg"} for root in roots
    ] + [{"path": fallback}]
    result["folders"] = {"data": folders, "logs": folders}
    result["saves"] = [
        f"{root}/{name}" for root in roots for name in ("USA", "EUR", "savestates")
    ]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True, choices=("windows", "linux", "macos"))
    parser.add_argument("--appimage-arch", choices=("x86_64", "aarch64"))
    parser.add_argument("output", type=Path, help="Manifest path inside the package staging directory")
    args = parser.parse_args()
    if args.appimage_arch and args.platform != "linux":
        parser.error("--appimage-arch requires --platform linux")
    result = profile(args.platform, args.appimage_arch)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
