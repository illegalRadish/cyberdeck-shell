#!/bin/bash
# Links every shader pair as GLSL ES 3.00, the way the Pi will.
#
# This exists because shader problems are invisible on macOS: the desktop build
# compiles GLSL 3.30 core and never exercises the ES path, so the first sign of
# trouble was the Pi restart-looping on
#
#     declarations for uniform `uRect` have mismatching precision qualifiers
#
# Linking is the important part — a precision mismatch compiles fine in each
# stage separately and only fails when the two are linked together.
#
# Skips cleanly when glslangValidator is absent (brew install glslang).
set -u

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJ" || exit 1

if ! command -v glslangValidator >/dev/null 2>&1; then
  echo "  SKIP  glslangValidator not installed (brew install glslang)"
  exit 0
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# Must mirror retargetVersion() in src/render/Shader.cpp. If that changes, this
# changes with it or the test stops meaning anything.
retarget() {
  {
    echo "#version 300 es"
    echo "precision highp float;"
    echo "precision highp int;"
    tail -n +2 "$1"
  } > "$2"
}

# Pairs as GLRenderer::init loads them; note scanlines reuses bg.vert.
PAIRS="rect:rect tex:tex bg:bg bg:scanlines"

fails=0
for pair in $PAIRS; do
  v="${pair%%:*}"
  f="${pair##*:}"
  retarget "assets/shaders/$v.vert" "$OUT/p.vert"
  retarget "assets/shaders/$f.frag" "$OUT/p.frag"

  if out="$(glslangValidator -l "$OUT/p.vert" "$OUT/p.frag" 2>&1)" \
     && ! printf '%s' "$out" | grep -qiE 'error|mismatch'; then
    printf "  ok    %s.vert + %s.frag links as ES 3.00\n" "$v" "$f"
  else
    printf " FAIL   %s.vert + %s.frag\n" "$v" "$f"
    printf '%s\n' "$out" | grep -iE 'error|mismatch' | head -3 | sed 's/^/        /'
    fails=$((fails + 1))
  fi
done

echo
if [ "$fails" -eq 0 ]; then
  echo "ALL PASSED (0 failures)"
else
  echo "FAILED ($fails failure(s))"
fi
exit "$fails"
