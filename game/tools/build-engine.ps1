param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$bash = "C:\msys64\usr\bin\bash.exe"
$rootMsys = $root.Path.Replace("\", "/")
if ($rootMsys -match '^([A-Za-z]):/(.*)$') {
    $rootMsys = "/$($Matches[1].ToLower())/$($Matches[2])"
}
$configure = @"
cd '$rootMsys' &&
export PATH=/mingw64/bin:/usr/bin:`$PATH &&
cmake -S engine -B build/engine-mingw64 -G Ninja -DCMAKE_BUILD_TYPE=$Configuration -DBUILD_STANDALONE=ON -DBUILD_SERVER=ON -DBUILD_GAME_QVMS=OFF -DBUILD_RENDERER_GL1=OFF -DBUILD_RENDERER_GL2=ON -DUSE_RENDERER_DLOPEN=OFF -DUSE_OPENAL=OFF -DUSE_HTTP=OFF -DUSE_CODEC_VORBIS=OFF -DUSE_CODEC_OPUS=ON -DUSE_VOIP=ON -DUSE_MUMBLE=OFF
"@
$build = @"
cd '$rootMsys' &&
export PATH=/mingw64/bin:/usr/bin:`$PATH &&
cmake --build build/engine-mingw64 --config $Configuration -j 8
"@

& $bash -lc $configure
if ($LASTEXITCODE -ne 0) { throw "Engine configure failed." }
& $bash -lc $build
if ($LASTEXITCODE -ne 0) { throw "Engine build failed." }
