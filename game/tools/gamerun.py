"""Helpers for the tools that run the packaged game on a map (map_tour.py, bot_soak.py).

They run the freshly compiled maps/<map>.bsp and .aas instead of the copies in pak0.pk3, so a map
can be checked straight after build-map-mac.sh without packaging. The files go into the run's own
home directory, which the game searches before pak0.pk3; the command needs sv_pure 0 so the client
takes them. Textures, shaders and sounds still come from pak0.pk3, so on macOS the package is
rebuilt first when anything under assets/testgame is newer than it.

Several of these tools can run at once (one agent per map, say), so packaging and the game window
are taken one at a time under a lock file.
"""
import contextlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist/FRAGNAROK"
PAK = DIST / "fragnarok/pak0.pk3"
LOCK = ROOT / "build/.game.lock"


@contextlib.contextmanager
def game_lock():
    """Wait for any other tour or packaging to finish."""
    LOCK.parent.mkdir(parents=True, exist_ok=True)
    with open(LOCK, "w") as f:
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(f.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(f, fcntl.LOCK_EX)
        yield


def refresh_package():
    """Rebuild pak0.pk3 if the assets have changed since it was built (macOS only); call under game_lock."""
    if not PAK.exists():
        sys.exit(f"No {PAK}; build and package the game first.")
    built = PAK.stat().st_mtime
    newer = next((p for p in (ROOT / "assets/testgame").rglob("*") if p.is_file() and p.stat().st_mtime > built), None)
    if not newer:
        return
    if sys.platform != "darwin":
        print(f"Warning: {newer.relative_to(ROOT)} is newer than pak0.pk3; run tools/package-runtime.ps1 to see it.")
        return
    print(f"Repackaging, since {newer.relative_to(ROOT)} changed...")
    subprocess.run(["bash", str(ROOT / "tools/build-mac.sh"), "--package-only"], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)


def stage_map(home, name):
    """Copy the compiled map into the run's home so the game loads it ahead of pak0.pk3."""
    maps = Path(home) / "fragnarok/maps"
    maps.mkdir(parents=True, exist_ok=True)
    for ext in ("bsp", "aas"):
        shutil.copy(ROOT / f"maps/{name}.{ext}", maps / f"{name}.{ext}")
    # A map's own shader file and texture folder go along too, so a new map shows its textures on
    # Windows before anything has been repackaged (the game reads loose files ahead of pak0.pk3)
    shader = ROOT / f"assets/testgame/scripts/{name}.shader"
    if shader.exists():
        (Path(home) / "fragnarok/scripts").mkdir(parents=True, exist_ok=True)
        shutil.copy(shader, Path(home) / f"fragnarok/scripts/{name}.shader")
    textures = ROOT / f"assets/testgame/textures/{name}"
    if textures.is_dir():
        shutil.copytree(textures, Path(home) / f"fragnarok/textures/{name}", dirs_exist_ok=True)
