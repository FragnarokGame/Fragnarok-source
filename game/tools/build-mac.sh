#!/usr/bin/env bash
# macOS equivalent of build.bat: builds the engine natively and packages
# dist/FRAGNAROK with the same layout as tools/package-runtime.ps1.
# Maps and generated art are committed, so this skips build-map.ps1.
#
# Requires: Xcode command line tools, cmake, ninja (brew install cmake ninja)
#
# Usage: tools/build-mac.sh [Release|Debug] [--engine-only|--package-only]

set -euo pipefail

CONFIG="Release"
STEP="all"
for arg in "$@"; do
    case "$arg" in
        Release|Debug|RelWithDebInfo) CONFIG="$arg" ;;
        --engine-only) STEP="engine" ;;
        --package-only) STEP="package" ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/engine-macos"
ENGINE_OUT="$BUILD/$CONFIG"
DIST="$ROOT/dist/FRAGNAROK"
BASE="$DIST/fragnarok"
PAK="$ROOT/build/pak0"

build_engine() {
    cmake -S "$ROOT/engine" -B "$BUILD" -G Ninja \
        -DCMAKE_BUILD_TYPE="$CONFIG" \
        -DBUILD_STANDALONE=ON -DBUILD_SERVER=ON -DBUILD_GAME_QVMS=OFF \
        -DBUILD_RENDERER_GL1=OFF -DBUILD_RENDERER_GL2=ON -DUSE_RENDERER_DLOPEN=OFF \
        -DUSE_OPENAL=OFF -DUSE_HTTP=OFF -DUSE_CODEC_VORBIS=OFF -DUSE_CODEC_OPUS=ON \
        -DUSE_VOIP=ON -DUSE_MUMBLE=OFF
    cmake --build "$BUILD" --config "$CONFIG" -j "$(sysctl -n hw.ncpu)"
}

package_runtime() {
    [ -d "$ENGINE_OUT/FRAGNAROK.app" ] || { echo "FRAGNAROK.app not found. Build the engine first." >&2; exit 1; }
    [ -f "$ENGINE_OUT/FRAGNAROKDed" ] || { echo "FRAGNAROKDed not found. Build the engine first." >&2; exit 1; }
    for map in test_chamber rift_foundry skyreach bastion station horologe gallows chopping_mall craterfall bifrost; do
        for ext in bsp aas; do
            [ -f "$ROOT/maps/$map.$ext" ] || { echo "maps/$map.$ext not found." >&2; exit 1; }
        done
    done

    rm -rf "$DIST" "$PAK"
    mkdir -p "$BASE" "$DIST/licenses"
    mkdir -p "$PAK"/{maps,scripts,textures/testgame,gfx/2d,gfx/damage,gfx/effects,menu/art,models/players,sound/player,icons,botfiles}

    local A="$ROOT/assets"
    local OA="$A/source/openarena"

    cp -R "$ENGINE_OUT/FRAGNAROK.app" "$DIST/"
    cp "$ENGINE_OUT/FRAGNAROKDed" "$DIST/"
    cp "$ROOT/README.md" "$DIST/"
    python3 "$ROOT/tools/copy-licenses.py" "$DIST/licenses"

    cp "$ENGINE_OUT"/fragnarok/{cgame,qagame,ui}.dylib "$BASE/"
    python3 "$ROOT/tools/copy-steam.py" macos "$DIST/FRAGNAROK.app"
    cp "$A/testgame/autoexec.cfg" "$A/testgame/server-1024.cfg" "$BASE/"
    python3 "$ROOT/tools/configure-auth.py" --runtime "$DIST" --if-configured

    cp "$A/testgame/default.cfg" "$PAK/"
    cp "$A/testgame/scripts"/{arsenal.shader,testgame.shader,skyreach.shader,castle.shader,station.shader,horologe.shader,gallows.shader,foundry.shader,mall.shader,craterfall.shader,bifrost.shader,shaderlist.txt,arenas.txt,bots.txt} "$PAK/scripts/"
    cp "$A/testgame/textures/testgame/"*.png "$PAK/textures/testgame/"
    mkdir -p "$PAK/textures/skyreach"
    cp "$A/testgame/textures/skyreach/"*.png "$PAK/textures/skyreach/"
    mkdir -p "$PAK/textures/castle" "$PAK/textures/craterfall" "$PAK/textures/bifrost"
    cp "$A/testgame/textures/castle/"*.png "$PAK/textures/castle/"
    cp "$A/testgame/textures/craterfall/"*.png "$PAK/textures/craterfall/"
    cp "$A/testgame/textures/bifrost/"*.png "$PAK/textures/bifrost/"
    mkdir -p "$PAK/textures/station"
    cp "$A/testgame/textures/station/"*.png "$PAK/textures/station/"
    mkdir -p "$PAK/textures/horologe"
    cp "$A/testgame/textures/horologe/"*.png "$PAK/textures/horologe/"
    mkdir -p "$PAK/textures/gallows"
    cp "$A/testgame/textures/gallows/"*.png "$A/testgame/textures/gallows/CREDITS.txt" "$PAK/textures/gallows/"
    mkdir -p "$PAK/textures/foundry"
    cp "$A/testgame/textures/foundry/"*.png "$A/testgame/textures/foundry/CREDITS.txt" "$PAK/textures/foundry/"
    mkdir -p "$PAK/textures/mall"
    cp "$A/testgame/textures/mall/"*.png "$PAK/textures/mall/"
    cp "$A/testgame/gfx/2d/"*.png "$PAK/gfx/2d/"
    cp "$A/testgame/menu/art/"*.png "$PAK/menu/art/"
    cp -R "$A/testgame/models/". "$PAK/models/"
    cp -R "$A/testgame/sound/". "$PAK/sound/"
    cp "$A/testgame/icons/"*.png "$PAK/icons/"
    cp "$A/testgame/gfx/damage/"*.png "$PAK/gfx/damage/"
    cp "$A/testgame/gfx/effects/"*.png "$PAK/gfx/effects/"
    # Images from create-missing-art.py that live outside the folders above.
    for dir in gfx/medals gfx/misc menu/tab sprites; do
        mkdir -p "$PAK/$dir"
        cp "$A/testgame/$dir/"*.png "$PAK/$dir/"
    done
    mkdir -p "$PAK/levelshots"
    for map in test_chamber rift_foundry skyreach bastion station horologe gallows chopping_mall craterfall bifrost; do
        cp "$A/testgame/levelshots/${map}_wide.jpg" "$PAK/levelshots/"
        cp "$ROOT/maps/$map.bsp" "$ROOT/maps/$map.aas" "$PAK/maps/"
    done
    cp -R "$OA/botfiles/". "$PAK/botfiles/"
    cp -R "$OA/models/players/skelebot" "$PAK/models/players/"
    cp -R "$OA/sound/player/skelebot" "$PAK/sound/player/"
    cp "$OA/scripts/player_skelebot.shader" "$PAK/scripts/"
    mkdir -p "$PAK/textures/effects"
    cp "$OA/textures/effects/tinfx3.jpg" "$PAK/textures/effects/"

    (cd "$PAK" && zip -q -9 -r -X "$BASE/pak0.pk3" . -x '.DS_Store' '*/.DS_Store')

    # The client carries its own data and modules, even when only the app is moved
    # or Gatekeeper translocates it. The sibling data remains for FRAGNAROKDed.
    python3 "$ROOT/tools/bundle-macos.py" --app "$DIST/FRAGNAROK.app" \
        --game-data "$BASE" --licenses "$DIST/licenses"

    echo "Packaged $DIST"
}

[ "$STEP" = "package" ] || build_engine
[ "$STEP" = "engine" ] || package_runtime
