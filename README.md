# FRAGNAROK (source code)

FRAGNAROK is an arena shooter built on the [ioquake3](https://ioquake3.org/)
engine (Quake III Arena GPL source). This repository holds the **engine and
game source code only**. The maps, textures, models, sounds, music and other
art are not included, so a build from this repository does not run as the full
game by itself. The finished game is distributed separately.

## What is here

- `game/engine/`: the engine and the FRAGNAROK game, cgame and UI code
  (`code/game`, `code/cgame`, `code/q3_ui`), plus shared libraries.
- `game/tools/`: scripts to build the engine on Windows and macOS.
- `LICENSE`: GNU GPL v2, which covers the source in this repository.

## Building the engine

- **Windows:** install MSYS2 and CMake, then run `game\tools\build-engine.ps1`.
- **macOS:** run `game/tools/build-mac.sh`.

The build produces the engine executable and the game libraries. To run it you
need your own game data.

## Licensing

The source code is licensed under the GPL v2 (see `LICENSE` and
`game/engine/COPYING.txt`). It builds on id Software's Quake III Arena source,
released under the GPL, and on the ioquake3 project. Third-party libraries keep
their own licenses, which live beside their code in `game/engine/code/thirdparty`.

The name FRAGNAROK and all game assets are not covered by the GPL and are not
part of this repository.

## Contributing

This is a periodically synced snapshot of the project's private working
repository, so pull requests can't be merged directly. Bug reports and
suggestions are welcome as issues.
