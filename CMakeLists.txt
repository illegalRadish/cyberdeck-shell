cmake_minimum_required(VERSION 3.16)
project(cyberdeck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Raspberry Pi cannot provide a desktop GL 3.3 core context (see
# src/render/GL.hpp), so Linux targets OpenGL ES 3.0. That also removes the need
# for an extension loader, which the plain <GL/gl.h> path would otherwise
# require to reach anything past GL 1.1. macOS keeps desktop GL 3.3 for
# development. Override with -DCYBERDECK_GLES=ON/OFF.
if(UNIX AND NOT APPLE)
    set(CYBERDECK_GLES_DEFAULT ON)
else()
    set(CYBERDECK_GLES_DEFAULT OFF)
endif()
option(CYBERDECK_GLES "Target OpenGL ES 3.0 instead of desktop OpenGL 3.3"
       ${CYBERDECK_GLES_DEFAULT})

find_package(SDL2 REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(SDL2_image REQUIRED)
if(NOT CYBERDECK_GLES)
    find_package(OpenGL REQUIRED)
endif()
find_package(SQLite3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED mpv)

set(CYBERDECK_SOURCES
    src/main.cpp
    src/core/Application.cpp
    src/core/Assets.cpp
    src/core/JsonLine.cpp
    src/ai/AiAssets.cpp
    src/platform/Window.cpp
    src/platform/Process.cpp
    src/render/GLRenderer.cpp
    src/render/Shader.cpp
    src/render/Texture.cpp
    src/render/Font.cpp
    src/render/TextLayout.cpp
    src/input/Input.cpp
    src/ui/Node.cpp
    src/ui/FocusNav.cpp
    src/ui/Screen.cpp
    src/ui/ScreenManager.cpp
    src/ui/Label.cpp
    src/ui/Card.cpp
    src/ui/Button.cpp
    src/ui/SystemHud.cpp
    src/ui/AsciiSpinner.cpp
    src/system/SystemSampler.cpp
    src/media/MediaTypes.cpp
    src/media/MediaRoot.cpp
    src/media/MediaDatabase.cpp
    src/media/ThumbnailCache.cpp
    src/media/MediaScanner.cpp
    src/media/MediaLibrary.cpp
    src/media/MpvPlayer.cpp
    src/net/TorrentFiler.cpp
    src/net/TorrentManager.cpp
    src/screens/HomeScreen.cpp
    src/screens/MediaScreen.cpp
    src/screens/PlaceholderScreen.cpp
    src/screens/LibraryBrowserScreen.cpp
    src/screens/MusicScreen.cpp
    src/screens/MoviesScreen.cpp
    src/screens/NowPlayingScreen.cpp
    src/screens/TerminalScreen.cpp
    src/screens/SettingsScreen.cpp
    src/screens/DemoScreen.cpp
    src/screens/AskDeckScreen.cpp
    src/screens/AiAssetsScreen.cpp
    src/screens/DownloadsScreen.cpp
    src/screens/TorrentSearchScreen.cpp
)

add_executable(cyberdeck ${CYBERDECK_SOURCES})

target_include_directories(cyberdeck PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
    ${MPV_INCLUDE_DIRS}
)

target_link_libraries(cyberdeck PRIVATE ${MPV_LIBRARIES})

# Imported targets are not guaranteed. Homebrew's CMake config packages export
# SDL2::SDL2 and SQLite3::SQLite3, but Debian's do not always, and a missing
# target is passed through to the linker verbatim — the failure is
# "cannot find -lSQLite3::SQLite3" at link time rather than anything at
# configure time. Resolve each to a real library the same way the SDL2_ttf and
# SDL2_image blocks below already do.
# pkg-config is the fallback rather than find_library because it supplies the
# include directories too, and both libsqlite3-dev and libsdl2-dev ship .pc
# files on Debian.
if(TARGET SQLite3::SQLite3)
    target_link_libraries(cyberdeck PRIVATE SQLite3::SQLite3)
    set(CYBERDECK_SQLITE_FROM "imported target")
else()
    pkg_check_modules(SQLITE3PC REQUIRED sqlite3)
    target_include_directories(cyberdeck PRIVATE ${SQLITE3PC_INCLUDE_DIRS})
    target_link_directories(cyberdeck PRIVATE ${SQLITE3PC_LIBRARY_DIRS})
    target_link_libraries(cyberdeck PRIVATE ${SQLITE3PC_LIBRARIES})
    set(CYBERDECK_SQLITE_FROM "pkg-config")
endif()

if(TARGET SDL2::SDL2)
    target_link_libraries(cyberdeck PRIVATE SDL2::SDL2)
    set(CYBERDECK_SDL_FROM "imported target")
else()
    pkg_check_modules(SDL2PC REQUIRED sdl2)
    target_include_directories(cyberdeck PRIVATE ${SDL2PC_INCLUDE_DIRS})
    target_link_directories(cyberdeck PRIVATE ${SDL2PC_LIBRARY_DIRS})
    target_link_libraries(cyberdeck PRIVATE ${SDL2PC_LIBRARIES})
    set(CYBERDECK_SDL_FROM "pkg-config")
endif()

if(CYBERDECK_GLES)
    find_library(CYBERDECK_GLESV2 NAMES GLESv2 REQUIRED)
    target_compile_definitions(cyberdeck PRIVATE CYBERDECK_USE_GLES=1)
    target_link_libraries(cyberdeck PRIVATE ${CYBERDECK_GLESV2})
    message(STATUS "cyberdeck: OpenGL ES 3.0 (Raspberry Pi / Linux)")
else()
    target_link_libraries(cyberdeck PRIVATE OpenGL::GL)
    message(STATUS "cyberdeck: desktop OpenGL 3.3 core")
endif()

target_compile_options(cyberdeck PRIVATE ${MPV_CFLAGS_OTHER})
target_link_directories(cyberdeck PRIVATE ${MPV_LIBRARY_DIRS})

if(TARGET SDL2_ttf::SDL2_ttf)
    target_link_libraries(cyberdeck PRIVATE SDL2_ttf::SDL2_ttf)
elseif(TARGET SDL2::SDL2_ttf)
    target_link_libraries(cyberdeck PRIVATE SDL2::SDL2_ttf)
else()
    find_library(SDL2_TTF_LIBRARY NAMES SDL2_ttf REQUIRED)
    target_link_libraries(cyberdeck PRIVATE ${SDL2_TTF_LIBRARY})
endif()

if(TARGET SDL2_image::SDL2_image)
    target_link_libraries(cyberdeck PRIVATE SDL2_image::SDL2_image)
elseif(TARGET SDL2::SDL2_image)
    target_link_libraries(cyberdeck PRIVATE SDL2::SDL2_image)
else()
    find_library(SDL2_IMAGE_LIBRARY NAMES SDL2_image REQUIRED)
    target_link_libraries(cyberdeck PRIVATE ${SDL2_IMAGE_LIBRARY})
endif()

if(APPLE)
    target_compile_definitions(cyberdeck PRIVATE GL_SILENCE_DEPRECATION=1)
endif()

target_compile_definitions(cyberdeck PRIVATE
    CYBERDECK_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

# Printed so a link failure on a new machine is diagnosable from the configure
# output rather than by guessing which package produced which target.
message(STATUS "cyberdeck: SQLite3 via ${CYBERDECK_SQLITE_FROM}")
message(STATUS "cyberdeck: SDL2 via ${CYBERDECK_SDL_FROM}")

add_custom_command(TARGET cyberdeck POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:cyberdeck>/assets
    COMMENT "Copying assets next to cyberdeck binary"
)
