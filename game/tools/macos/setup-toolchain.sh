#!/usr/bin/env bash
# Build native macOS q3map2 and mbspc from the same NetRadiant-custom commit the
# Windows toolchain uses (68ecbed, release 20260114), into tools/macos-toolchain/bin.
#
# Requires Xcode command line tools and Homebrew. Safe to rerun.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TC="$ROOT/tools/macos-toolchain"
SRC="$TC/src/netradiant-custom"
COMMIT=68ecbed6

brew install glib libxml2 libpng jpeg-turbo pkgconf assimp minizip

if [ ! -d "$SRC/.git" ]; then
    git clone https://github.com/Garux/netradiant-custom.git "$SRC"
fi
cd "$SRC"
git checkout -q "$COMMIT"
# clang/libc++ build fix, arm64 platform block for mbspc, and 64 MB thread
# stacks (macOS gives std::thread 512 KB, which crashes -vis and -light)
if git apply --check "$ROOT/tools/macos/netradiant-custom-macos.patch" 2>/dev/null; then
    git apply "$ROOT/tools/macos/netradiant-custom-macos.patch"
fi

export CPPFLAGS="-I/opt/homebrew/include" LDFLAGS="-L/opt/homebrew/lib"
DEPENDENCIES_CHECK=off make -j"$(sysctl -n hw.ncpu)" binaries-q3map2 binaries-mbspc

mkdir -p "$TC/bin"
cp install/q3map2.* "$TC/bin/q3map2"
cp install/mbspc.* "$TC/bin/mbspc"
"$TC/bin/q3map2" -help 2>&1 | grep -m1 "2.5.17"
echo "Installed q3map2 and mbspc to $TC/bin"
