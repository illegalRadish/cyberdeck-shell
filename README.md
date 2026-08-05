# Cyberdeck Shell

Lightweight, hardware-accelerated media shell for a Raspberry Pi cyberdeck.
Developed on macOS first; deploys later to Raspberry Pi OS Lite.

## Stack

- C++20, SDL2, OpenGL 3.3, SDL2_ttf/image, SQLite3, **libmpv**

## Media library (`PI LIB`)

Auto-discovers a folder named **`PI LIB`** on Mac and Pi (or set `CYBERDECK_MEDIA_ROOT`).

```text
PI LIB/
  Music/  Movies/  TV Shows/  Photos/  Videos/  Books/  ROMs/  Downloads/
  .cyberdeck/library.db
  .cyberdeck/thumbs/
```

## Build (macOS)

```bash
brew install sdl2 sdl2_image sdl2_ttf cmake sqlite mpv
mkdir -p build && cd build
cmake ..
make
./cyberdeck
```

## Controls

- Arrows / WASD — navigate
- Enter — open / play / pause
- ←/→ on now playing — seek ±10s
- Esc — back (quits on home); cancels a running assistant query

## Ask the Deck (offline voice assistant)

Home → **Ask the Deck**. A single conversation screen: chat log on the left, an
ASCII robot on the right that mouths along while it talks. Everything is local.

    record (arecord/sox) → whisper.cpp → libzim → ollama → piper → aplay/afplay

The prompt is always live, so there are no modes to pick:

- **Type and press Enter** to send a question
- **Press Enter on an empty prompt** to talk instead; Enter again stops recording (30s cap)
- **Up/Down** scrolls the conversation, **Esc** cancels a turn in flight (and only leaves the screen when idle)

Replies stream in word by word rather than appearing all at once, and the robot's
mouth animates only while Piper is actually speaking.

Retrieval is passage-level, not "first paragraph of the top hit": the index is
queried with content words only (the "what is" opener measurably hurts ranking),
the top articles are split into paragraphs, and the best few passages across them
are injected with their article names shown under the reply.

**Nothing runs resident.** Each query spawns `assets/ai/ask_deck.py`, which exits
when the interaction ends. Stages are strictly serialised — whisper exits before
ollama starts and ollama unloads (`keep_alive: "0s"`) before piper runs — so peak
RSS is the largest single stage (~800MB with `qwen2.5:0.5b`), not the sum. Do not
pipeline them; that invariant is what makes this fit in 4GB.

Temp audio goes to `/dev/shm` on Linux, so a query writes nothing to the SD card.

### Assets

Settings → **AI ASSETS** shows what's present and downloads what isn't
(resumable, idempotent, per-asset progress). Roughly 540MB without the ZIM,
1.5GB with it.

```text
~/.local/share/cyberdeck/ai/         # SD card — small models
  models/ggml-tiny.en.bin            # whisper
  voices/en_US-amy-low.onnx(.json)   # piper
  logs/last_run.log                  # last query's diagnostics
PI LIB/.cyberdeck/ai/zim/            # media drive — the ~1GB ZIM
~/.ollama/                           # ollama's own store
```

The ZIM is **optional**: without it answers still work, just ungrounded. The
whisper/piper/ollama binaries are installed on the device, not downloaded —
the screen reports them as "INSTALL ON DEVICE".

To move up a size, edit the one table at the top of `src/ai/AiAssets.cpp`
(`ggml-base.en.bin`, `qwen2.5:1.5b`) — both are single-line changes.

The ZIM URL pins a dated Kiwix build (`..._2026-05.zim`) because the undated
alias 404s. When that month is retired, list
<https://download.kiwix.org/zim/wikipedia/> and bump `kZimFile`/`kZimUrl`; the
row's failure note prints the URL so a rotted link is obvious.

`expectedBytes` in that table is **display only** — the up-front size estimate.
Success is decided by curl's exit status and, when the server provides one, its
`Content-Length`. A stale constant may make a progress bar wrong; it must never
reject a file that downloaded correctly.

### macOS dev setup

```bash
brew install ollama whisper-cpp sox
pip3 install --user piper-tts libzim

# Symlink the binaries into ai/bin. This is not optional: a GUI-launched app on
# macOS inherits a minimal PATH with no /opt/homebrew, and pip puts `piper` in
# ~/Library/Python/<ver>/bin which is never on PATH. AiAssets prefers ai/bin
# over PATH precisely so this works.
BIN=~/.local/share/cyberdeck/ai/bin; mkdir -p "$BIN"
ln -sf "$(brew --prefix whisper-cpp)/bin/whisper-cli" "$BIN/whisper-cli"
ln -sf ~/Library/Python/3.9/bin/piper                 "$BIN/piper"
ln -sf /opt/homebrew/bin/ollama                       "$BIN/ollama"
```

Do **not** `brew services start ollama` — the helper runs its own short-lived
server so nothing sits resident. Then Settings → AI ASSETS → DOWNLOAD MISSING
pulls the LLM through the shell itself.

## Deploying to a Raspberry Pi

### What to flash

**Raspberry Pi OS (64-bit), Bookworm or newer**, via Raspberry Pi Imager, on a
Pi 4 or Pi 5. 64-bit matters: the prebuilt `libmpv` and the AI stack are arm64.

**Lite is the right choice** — this shell replaces the desktop rather than
running inside one, and on Lite SDL renders straight to KMS/DRM with no X or
Wayland underneath. The Desktop image also works if you would rather have a
fallback GUI.

### Graphics: the Pi cannot do desktop GL 3.3

The VideoCore GPU has no desktop OpenGL 3.3 — Mesa's V3D driver exposes OpenGL
ES 3.1, and desktop GL only up to 2.1-3.1. Requesting a 3.3 core context there
fails outright rather than degrading, so **Linux builds target GLES 3.0**:
`CYBERDECK_GLES` defaults ON everywhere except macOS. Shaders are authored once
in GLSL 3.30 core and retargeted to GLSL ES 3.00 at load time
(`Shader.cpp: retargetVersion`), so there is only ever one copy of each.

Nothing else in the renderer changes: vertex array objects, `GL_CLAMP_TO_EDGE`,
unsized `GL_RGBA` internal formats and `GL_UNPACK_ALIGNMENT` are all core in
GLES 3.0. GLES also removes the need for an extension loader, since
`<GLES3/gl3.h>` declares the whole API while Linux's `<GL/gl.h>` stops at 1.1.

### Build

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config git python3 \
    libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev \
    libsqlite3-dev libmpv-dev libgles2-mesa-dev

git clone <your-repo-url> cyberdeck-shell && cd cyberdeck-shell
cmake -S . -B build          # prints "cyberdeck: OpenGL ES 3.0"
cmake --build build -j4
```

### Run

On Lite there is no display server, so point SDL at KMS/DRM:

```bash
sudo usermod -aG video,render,input "$USER"   # log out and back in
SDL_VIDEODRIVER=kmsdrm ./build/cyberdeck
```

Put `PI LIB` on the media drive; it is auto-discovered under `/media`, `/mnt`
and `/run/media`, or set `CYBERDECK_MEDIA_ROOT` explicitly.

### Start at boot

```bash
sudo ./deploy/install-service.sh --media-root "/media/$USER/PI LIB"
sudo systemctl start cyberdeck        # or just reboot
journalctl -u cyberdeck -f            # follow it
```

Two things in `deploy/cyberdeck.service` are load-bearing, and both cause the
same symptom if missed — the service starts, fails silently, and restarts:

- **`TTYPath=/dev/tty1` + `StandardInput=tty`.** KMS/DRM only grants DRM master
  to a process attached to an active VT. Without them SDL cannot become DRM
  master and exits immediately.
- **`getty@tty1` must be disabled**, or it holds that VT. The installer does it.

`SupplementaryGroups=video render input` covers `/dev/dri/*` and evdev; the
installer also adds the login user to those groups for running by hand.

To undo:

```bash
sudo systemctl disable --now cyberdeck
sudo systemctl enable --now getty@tty1
```

The torrent daemon needs no unit of its own — `TorrentManager` starts it when
the Downloads screen first polls and finds RPC unreachable.

### Torrent engine

```bash
bash assets/net/install-torrent-engine.sh --media-root "/media/$USER/PI LIB"
```

Installs and configures `transmission-daemon` as a user-level instance. It is
deliberately **not** the packaged system service: that runs as
`debian-transmission`, which cannot write to a USB drive mounted for the login
user, and the failure surfaces only as a permission error in `journalctl`.

The daemon is started `--foreground` under `nohup` rather than in its own daemon
mode. On macOS its fork-without-exec kills it the moment a magnet is parsed;
Linux is unaffected, but one launch path serves both.

### Pi bring-up

```bash
sudo apt install alsa-utils
arecord -l                                   # note the device; set CYBERDECK_AUDIO_DEVICE
arecord -d 3 -f S16_LE -r 16000 t.wav && aplay t.wav

pip install piper-tts libzim --break-system-packages
git clone https://github.com/ggerganov/whisper.cpp && cd whisper.cpp
cmake -B build -DGGML_NATIVE=ON && cmake --build build -j4   # symlink whisper-cli into ai/bin

curl -fsSL https://ollama.com/install.sh | sh
sudo systemctl disable --now ollama          # the helper runs its own short-lived server

python3 -u assets/ai/ask_deck.py --selftest  # the readiness checklist
python3 -u assets/ai/ask_deck.py --text "what is a transistor"
```

`ask_deck.py` defaults every path, so it runs standalone over SSH for debugging.
`--selftest` is what Settings → AI ASSETS → VERIFY shells out to.

If the ollama systemd unit is left enabled, the helper detects the running server
and does not own it; `keep_alive: "0s"` still frees the weights, leaving only the
~40MB idle server.

## Tests

```bash
./tests/run_tests.sh          # process, wrap, assets, screen
./tests/run_tests.sh screen   # one suite
```

Plain `main()` harnesses, no framework. They use a throwaway `HOME` and
`assets/ai/mock_deck.py` — a stage simulator with `--fail no_mic|no_ollama|crash|
garbage` — so the whole UI and process-supervision path is exercised with no
models, no mic, and no network.

## Phase status

- Phases 1–4: engine, UI, home, PI LIB index
- Phase 5: libmpv playback, music queue, movies + continue watching
- Phase 6: Ask the Deck — offline STT/RAG/LLM/TTS, asset downloader
