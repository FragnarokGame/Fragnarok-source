set(PROJECT_NAME FRAGNAROK)
# The release version lives in VERSION at the repository root
file(STRINGS ${CMAKE_SOURCE_DIR}/../../VERSION PROJECT_VERSION LIMIT_COUNT 1)

set(SERVER_NAME FRAGNAROKDed)
set(CLIENT_NAME FRAGNAROK)

set(BASEGAME fragnarok)

set(CGAME_MODULE cgame)
set(GAME_MODULE qagame)
set(UI_MODULE ui)

set(WINDOWS_ICON_PATH ${CMAKE_SOURCE_DIR}/misc/windows/quake3.ico)

set(MACOS_ICON_PATH ${CMAKE_SOURCE_DIR}/misc/macos/quake3_flat.icns)
set(MACOS_BUNDLE_ID org.ioquake.${CLIENT_NAME})

set(COPYRIGHT "FRAGNAROK based on Quake III Arena GPL engine technology.")

set(CONTACT_EMAIL "dev@example.invalid")
set(PROTOCOL_HANDLER_SCHEME fragnarok)
