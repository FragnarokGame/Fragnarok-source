#!/usr/bin/env python3
"""Copy every license and credits file a FRAGNAROK build must ship.

All packagers (package-runtime.ps1, build-mac.sh, package-test-release.py) call
this, so the list lives in one place. A missing source file is an error, not a
warning: shipping a binary without these notices breaks the GPL, zlib, BSD and
IJG terms the bundled code is used under.

    python copy-licenses.py <licenses folder>
    python copy-licenses.py --check <licenses folder>
"""

import argparse
from pathlib import Path
import shutil
import sys

GAME = Path(__file__).resolve().parents[1]
LICENSES = GAME / "assets/source/licenses"

# Shipped name -> source file. Keep THIRD-PARTY-NOTICES.txt and the in-game
# credits (q3_ui/ui_credits.c) in step with this list.
FILES = {
    "THIRD-PARTY-NOTICES.txt": LICENSES / "THIRD-PARTY-NOTICES.txt",
    "COPYING-ENGINE.txt": GAME / "engine/COPYING.txt",
    "LICENSE-SDL2.txt": LICENSES / "LICENSE-SDL2.txt",
    "LICENSE-ZLIB.txt": LICENSES / "LICENSE-ZLIB.txt",
    "README-LIBJPEG.txt": LICENSES / "README-LIBJPEG.txt",
    "COPYING-LIBOGG.txt": LICENSES / "COPYING-LIBOGG.txt",
    "COPYING-OPUS.txt": LICENSES / "COPYING-OPUS.txt",
    "COPYING-OPUSFILE.txt": LICENSES / "COPYING-OPUSFILE.txt",
    "LICENSE-AITOOLKIT.txt": GAME / "engine/code/thirdparty/aitoolkit/LICENSE.txt",
    "COPYING-OPENARENA.txt": GAME / "assets/source/openarena/COPYING",
    "CREDITS-OPENARENA.txt": GAME / "assets/source/openarena/CREDITS",
    "CREDITS-AEGIS.txt": GAME / "assets/source/openarena/CREDITS-AEGIS.txt",
    "CREDITS-HIGH-SEAS-TEXTURES.txt": GAME / "assets/testgame/textures/gallows/CREDITS.txt",
    "LICENSE-OXANIUM.txt": GAME / "assets/source/fonts/oxanium/OFL.txt",
}


def copy_licenses(destination):
    destination = Path(destination)
    missing = [str(source) for source in FILES.values() if not source.is_file()]
    if missing:
        raise SystemExit("License sources missing:\n  " + "\n  ".join(missing))
    destination.mkdir(parents=True, exist_ok=True)
    for name, source in FILES.items():
        shutil.copyfile(source, destination / name)
    check_licenses(destination)


def check_licenses(destination):
    destination = Path(destination)
    missing = [name for name in FILES if not (destination / name).is_file() or (destination / name).stat().st_size == 0]
    if missing:
        raise SystemExit(f"{destination} is missing license files: " + ", ".join(missing))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("destination", type=Path)
    parser.add_argument("--check", action="store_true", help="only verify an existing licenses folder")
    args = parser.parse_args()
    if args.check:
        check_licenses(args.destination)
    else:
        copy_licenses(args.destination)
    print(f"Licenses OK: {len(FILES)} files in {args.destination}")


if __name__ == "__main__":
    sys.exit(main())
