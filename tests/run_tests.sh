#!/bin/bash
# Standalone harnesses for the Ask-the-Deck pieces. No CTest, no framework —
# each file is a main() that prints ok/FAIL lines and exits non-zero on failure.
#
#   ./tests/run_tests.sh            # all
#   ./tests/run_tests.sh process    # one
#
# Run from the project root (assets/ paths are relative).
set -u

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJ" || exit 1
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

WHICH="${1:-all}"
CXX_FLAGS="-std=c++20 -Wall -Wextra -DGL_SILENCE_DEPRECATION=1 -Isrc"
SDL_CFLAGS="$(pkg-config --cflags sdl2 SDL2_ttf SDL2_image)"
SDL_LIBS="$(pkg-config --libs sdl2 SDL2_ttf SDL2_image)"
GL_LIBS="-framework OpenGL"
if [ "$(uname)" != "Darwin" ]; then GL_LIBS="-lGL"; fi

fails=0

build_and_run() {
  local name="$1"; shift
  local runner="$1"; shift
  echo
  echo "############ $name ############"
  # shellcheck disable=SC2086
  clang++ $CXX_FLAGS $SDL_CFLAGS -o "$OUT/$name" "tests/test_$name.cpp" "$@" \
    $SDL_LIBS $GL_LIBS 2> "$OUT/$name.build" || {
      echo "BUILD FAILED"; sed -n '1,40p' "$OUT/$name.build"; fails=$((fails+1)); return; }
  "$runner" "$OUT/$name" || fails=$((fails+1))
}

plain() { "$1" assets/ai/mock_deck.py; }

# The screen and asset harnesses need a throwaway HOME: AiAssets refuses to run
# unless every required model is present, so a fake tree of zero-byte stand-ins
# and stub binaries is built here. The real ~/.local/share/cyberdeck is untouched.
fakehome() {
  local FAKE="$OUT/fakehome"
  local AID="$FAKE/.local/share/cyberdeck/ai"
  mkdir -p "$AID/models" "$AID/voices" "$AID/bin" "$AID/logs" "$FAKE/stubbin" \
           "$FAKE/.ollama/models/manifests/registry.ollama.ai/library/qwen2.5/0.5b"
  : > "$AID/models/ggml-tiny.en.bin"
  : > "$AID/voices/en_US-amy-low.onnx"
  : > "$AID/voices/en_US-amy-low.onnx.json"
  echo '{}' > "$FAKE/.ollama/models/manifests/registry.ollama.ai/library/qwen2.5/0.5b/manifest"
  for b in whisper-cli piper ollama; do
    printf '#!/bin/sh\necho "%s stub 0.0"\n' "$b" > "$FAKE/stubbin/$b"
    chmod +x "$FAKE/stubbin/$b"
  done
  HOME="$FAKE" PATH="$FAKE/stubbin:$PATH" TMPDIR="$OUT/tmp" SDL_VIDEODRIVER=dummy \
    CYBERDECK_AI_HELPER="$PROJ/assets/ai/mock_deck.py" \
    "$1" assets/ai/mock_deck.py
}

if [ "$WHICH" = all ] || [ "$WHICH" = process ]; then
  build_and_run process plain src/platform/Process.cpp src/core/JsonLine.cpp
fi

if [ "$WHICH" = all ] || [ "$WHICH" = wrap ]; then
  build_and_run wrap plain src/render/TextLayout.cpp src/render/Font.cpp src/render/Texture.cpp
fi

if [ "$WHICH" = all ] || [ "$WHICH" = assets ]; then
  build_and_run assets fakehome src/ai/AiAssets.cpp src/platform/Process.cpp \
    src/core/JsonLine.cpp src/core/Assets.cpp
fi

# Not a build_and_run suite: it shells out to glslangValidator rather than
# compiling C++, and it is the only check that exercises the GLES shader path
# the Pi uses. macOS builds never touch it otherwise.
if [ "$WHICH" = all ] || [ "$WHICH" = shaders ]; then
  echo
  echo "############ shaders ############"
  ./tests/validate_shaders.sh || fails=$((fails+1))
fi

if [ "$WHICH" = all ] || [ "$WHICH" = torrent_filer ]; then
  build_and_run torrent_filer plain src/net/TorrentFiler.cpp src/media/MediaTypes.cpp
fi

if [ "$WHICH" = all ] || [ "$WHICH" = screen ]; then
  build_and_run screen fakehome \
    src/screens/AskDeckScreen.cpp src/screens/AiAssetsScreen.cpp src/ai/AiAssets.cpp \
    src/platform/Process.cpp src/core/JsonLine.cpp src/core/Assets.cpp \
    src/render/TextLayout.cpp src/render/Font.cpp src/render/Texture.cpp src/input/Input.cpp \
    src/ui/Screen.cpp src/ui/ScreenManager.cpp src/ui/Node.cpp src/ui/FocusNav.cpp
fi

echo
if [ "$fails" -eq 0 ]; then
  echo "==== all suites passed ===="
else
  echo "==== $fails suite(s) failed ===="
fi
exit "$fails"
