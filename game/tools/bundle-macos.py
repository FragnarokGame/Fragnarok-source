"""Finish a self-contained Mac app, then sign and verify its complete contents."""

import argparse
from pathlib import Path
import shutil
import subprocess
import zipfile


MODULES = ("cgame", "qagame", "ui")


def finish_bundle(app, game_data=None, licenses=None, universal=False):
    resources = app / "Contents/Resources/fragnarok"
    modules = app / "Contents/Frameworks/fragnarok"
    if game_data is not None:
        # Keep native code in Frameworks, separate from sealed data resources.
        shutil.copytree(game_data, resources, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("*.dylib"))
        modules.mkdir(parents=True, exist_ok=True)
        for name in MODULES:
            shutil.copy2(game_data / f"{name}.dylib", modules / f"{name}.dylib")
    if licenses is not None:
        shutil.copytree(licenses, app / "Contents/Resources/licenses", dirs_exist_ok=True)

    binary = app / "Contents/MacOS/FRAGNAROK"
    for required in (app / "Contents/Info.plist", binary, resources / "autoexec.cfg",
                     *(modules / f"{name}.dylib" for name in MODULES)):
        if not required.is_file():
            raise FileNotFoundError(required)
    with zipfile.ZipFile(resources / "pak0.pk3") as archive:
        if "default.cfg" not in archive.namelist() or archive.testzip() is not None:
            raise ValueError("The bundled pak0.pk3 must be intact and contain default.cfg")

    libraries = sorted(app.rglob("*.dylib"))
    if universal:
        for path in (binary, *libraries):
            architectures = subprocess.check_output(["lipo", "-archs", str(path)], text=True).split()
            if not {"arm64", "x86_64"}.issubset(architectures):
                raise ValueError(f"Not universal: {path}")
    # Seal the bundle only after data, login settings, modules and licenses are final.
    for library in libraries:
        subprocess.run(["codesign", "--force", "--sign", "-", str(library)], check=True)
    subprocess.run(["codesign", "--force", "--sign", "-", "--identifier",
                    "org.ioquake.FRAGNAROK", str(app)], check=True)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--game-data", type=Path)
    parser.add_argument("--licenses", type=Path)
    parser.add_argument("--universal", action="store_true")
    args = parser.parse_args()
    finish_bundle(args.app, args.game_data, args.licenses, args.universal)
    print(f"Packaged and signature-verified self-contained app: {args.app}")


if __name__ == "__main__":
    main()
