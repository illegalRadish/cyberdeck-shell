#!/bin/bash
# Installs and configures Transmission as the cyberdeck's torrent engine.
#
#   ./scripts/install-torrent-engine.sh [--media-root /path/to/PI\ LIB]
#
# Idempotent: safe to re-run. Every failure path prints one readable line as the
# LAST line of output, because DownloadsScreen shows exactly that line.
#
# WHY A USER-LEVEL DAEMON, NOT THE PACKAGED SYSTEM SERVICE
#     Debian's transmission-daemon package runs as the `debian-transmission`
#     user. PI LIB is typically a USB drive mounted for the login user, so that
#     service cannot write to it — the download silently fails with a permission
#     error buried in journalctl. Running as the current user with an explicit
#     --config-dir sidesteps the whole mess, so the packaged service is stopped
#     and disabled here rather than configured.
set -u

CONFIG_DIR="$HOME/.config/transmission-daemon"
MEDIA_ROOT="${CYBERDECK_MEDIA_ROOT:-}"

while [ $# -gt 0 ]; do
  case "$1" in
    --media-root) MEDIA_ROOT="${2:-}"; shift 2 ;;
    --config-dir) CONFIG_DIR="${2:-}"; shift 2 ;;
    *) echo "unknown argument: $1"; exit 2 ;;
  esac
done

die() { echo "$1"; exit 1; }

# ---------------------------------------------------------------- media root
# Mirrors MediaRoot.cpp's search well enough for the common cases. The shell
# passes --media-root explicitly, so this only matters when run by hand.
if [ -z "$MEDIA_ROOT" ]; then
  for candidate in "$PWD/PI LIB" "$PWD/../PI LIB" "$HOME/PI LIB" \
                   "$HOME/Desktop/PI LIB" "$HOME/Documents/PI LIB"; do
    if [ -d "$candidate" ]; then MEDIA_ROOT="$candidate"; break; fi
  done
fi
[ -n "$MEDIA_ROOT" ] || die "PI LIB not found — pass --media-root /path/to/PI LIB"
[ -d "$MEDIA_ROOT" ] || die "not a directory: $MEDIA_ROOT"

DOWNLOADS="$MEDIA_ROOT/Downloads"
COMPLETE="$DOWNLOADS/complete"
INCOMPLETE="$DOWNLOADS/incomplete"
WATCH="$DOWNLOADS/.watch"

command -v python3 >/dev/null 2>&1 || die "python3 is required but not installed"

# ------------------------------------------------------------------- install
if ! command -v transmission-daemon >/dev/null 2>&1; then
  echo "installing transmission…"
  if [ "$(uname)" = "Darwin" ]; then
    command -v brew >/dev/null 2>&1 || die "Homebrew not found — install it from brew.sh first"
    brew install transmission-cli || die "brew install transmission-cli failed"
  else
    if command -v apt-get >/dev/null 2>&1; then
      SUDO=""
      [ "$(id -u)" -eq 0 ] || SUDO="sudo"
      $SUDO apt-get update -qq
      $SUDO apt-get install -y transmission-daemon transmission-cli \
        || die "apt-get install transmission-daemon failed"
    else
      die "no supported package manager — install transmission-daemon by hand"
    fi
  fi
fi
command -v transmission-daemon >/dev/null 2>&1 \
  || die "transmission-daemon still not on PATH after install"

# --------------------------------------------------------- stop before write
# Transmission rewrites settings.json from memory when it exits, so anything
# written underneath a live daemon is silently discarded on the next shutdown.
# Stop first, always.
if [ "$(uname)" != "Darwin" ] && command -v systemctl >/dev/null 2>&1; then
  # The packaged service would hold port 9091 against our user-level instance.
  sudo systemctl stop transmission-daemon 2>/dev/null
  sudo systemctl disable transmission-daemon 2>/dev/null
fi
if pgrep -x transmission-daemon >/dev/null 2>&1; then
  echo "stopping running transmission-daemon…"
  pkill -x transmission-daemon 2>/dev/null
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    pgrep -x transmission-daemon >/dev/null 2>&1 || break
    sleep 0.5
  done
  pgrep -x transmission-daemon >/dev/null 2>&1 && die "could not stop the running transmission-daemon"
fi

# ---------------------------------------------------------------- directories
mkdir -p "$COMPLETE" "$INCOMPLETE" "$WATCH" "$CONFIG_DIR" \
  || die "could not create download directories under $DOWNLOADS"

# --------------------------------------------------------------------- config
# Merged into any existing settings.json rather than overwritten, so a
# hand-tuned speed limit or port-forward survives a re-run.
SETTINGS="$CONFIG_DIR/settings.json"
python3 - "$SETTINGS" "$COMPLETE" "$INCOMPLETE" "$WATCH" <<'PY' || die "could not write settings.json"
import json, os, sys

path, complete, incomplete, watch = sys.argv[1:5]

current = {}
if os.path.exists(path):
    try:
        with open(path) as fh:
            current = json.load(fh)
    except (ValueError, OSError):
        current = {}   # corrupt file: start clean rather than refuse to install

current.update({
    "download-dir": complete,
    "incomplete-dir": incomplete,
    "incomplete-dir-enabled": True,
    "watch-dir": watch,
    "watch-dir-enabled": True,
    # Localhost only, no credentials. The RPC port is never exposed off-device,
    # so authentication would be one more thing to store and get wrong.
    "rpc-enabled": True,
    "rpc-port": 9091,
    "rpc-bind-address": "127.0.0.1",
    "rpc-whitelist": "127.0.0.1",
    "rpc-whitelist-enabled": True,
    "rpc-authentication-required": False,
    # Stop seeding eventually so a Pi on a home connection does not upload
    # forever; the filer relies on the torrent staying registered until then.
    "ratio-limit": 2.0,
    "ratio-limit-enabled": True,
    # Renaming partials keeps a half-file from ever looking like playable media
    # to MediaScanner, which walks everything under PI LIB.
    "rename-partial-files": True,
    "start-added-torrents": True,
})

with open(path, "w") as fh:
    json.dump(current, fh, indent=4, sort_keys=True)
PY

# --------------------------------------------------------------------- launch
# --foreground under nohup, backgrounded — NOT transmission's own daemon mode.
#
# transmission-daemon daemonises by forking without exec'ing. On macOS, parsing
# a magnet converts its display name to UTF-8 through Objective-C, and touching
# the ObjC runtime in a forked-but-not-exec'd child aborts:
#
#     SIGABRT / OBJC / "*** multi-threaded process forked ***"
#
# The daemon then dies on the first torrent added, while list calls keep working
# — which presents as a network problem rather than a crash. --foreground never
# forks, so it cannot happen.
#
# Output goes to /dev/null because this script runs under Process, and a daemon
# holding our stdout pipe open means the parent never sees EOF.
echo "starting transmission-daemon…"
nohup transmission-daemon --foreground --config-dir "$CONFIG_DIR" >/dev/null 2>&1 &

# The daemon forks immediately but the RPC socket takes a moment to bind, so
# report success only once it actually answers.
for _ in $(seq 1 20); do
  if curl -s -o /dev/null "http://127.0.0.1:9091/transmission/rpc"; then
    echo "torrent engine ready"
    exit 0
  fi
  sleep 0.5
done
die "transmission-daemon started but RPC on 127.0.0.1:9091 never answered"
