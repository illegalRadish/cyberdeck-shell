cmake_minimum_required(VERSION 3.16)
project(cyberdeck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(OpenGL REQUIRED)
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

target_link_libraries(cyberdeck PRIVATE
    SDL2::SDL2
    OpenGL::GL
    SQLite3::SQLite3
    ${MPV_LIBRARIES}
)

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

add_custom_command(TARGET cyberdeck POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:cyberdeck>/assets
    COMMENT "Copying assets next to cyberdeck binary"
)
