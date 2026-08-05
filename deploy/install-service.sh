#!/bin/bash
# Installs the cyberdeck shell as a systemd service that starts at boot.
#
#   sudo ./deploy/install-service.sh [--media-root "/media/pi/PI LIB"]
#
# Run on the Pi, from the repo checkout. Idempotent: safe to re-run after a
# rebuild or a path change.
set -eu

die() { echo "$1" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "run with sudo"
[ "$(uname)" = "Linux" ] || die "this installs a systemd unit; run it on the Pi"

# The invoking user, not root — the shell must run as a normal user so it can
# reach the media drive and the per-user transmission config.
RUN_USER="${SUDO_USER:-}"
[ -n "$RUN_USER" ] || die "could not determine the login user; run via sudo, not as root directly"

APPDIR="$(cd "$(dirname "$0")/.." && pwd)"
MEDIA_ROOT=""

while [ $# -gt 0 ]; do
  case "$1" in
    --media-root) MEDIA_ROOT="${2:-}"; shift 2 ;;
    *) die "unknown argument: $1" ;;
  esac
done

[ -x "$APPDIR/build/cyberdeck" ] \
  || die "no binary at $APPDIR/build/cyberdeck — run: cmake -S . -B build && cmake --build build -j4"

if [ -z "$MEDIA_ROOT" ]; then
  for candidate in "/media/$RUN_USER/PI LIB" "/mnt/PI LIB" "/home/$RUN_USER/PI LIB" "$APPDIR/../PI LIB"; do
    if [ -d "$candidate" ]; then MEDIA_ROOT="$candidate"; break; fi
  done
fi
[ -n "$MEDIA_ROOT" ] || die "PI LIB not found — pass --media-root \"/media/$RUN_USER/PI LIB\""
[ -d "$MEDIA_ROOT" ] || die "not a directory: $MEDIA_ROOT"

# DRM master is only granted on an active VT, and getty owns tty1 by default.
# Leaving it enabled is why the service otherwise starts and immediately exits.
echo "disabling getty on tty1 (the shell takes that VT)…"
systemctl disable --now getty@tty1.service 2>/dev/null || true

usermod -aG video,render,input "$RUN_USER"

UNIT=/etc/systemd/system/cyberdeck.service
sed -e "s|__USER__|$RUN_USER|g" \
    -e "s|__APPDIR__|$APPDIR|g" \
    -e "s|__MEDIAROOT__|$MEDIA_ROOT|g" \
    "$APPDIR/deploy/cyberdeck.service" > "$UNIT"

# Wait for the media drive rather than racing it. Escaping the path is what
# systemd-escape is for; a hand-written unit with a space in it silently fails.
MOUNT_UNIT="$(systemd-escape -p --suffix=mount "$MEDIA_ROOT" 2>/dev/null || true)"
if [ -n "$MOUNT_UNIT" ] && mountpoint -q "$MEDIA_ROOT" 2>/dev/null; then
  sed -i "/^Wants=network-online.target/a RequiresMountsFor=$MEDIA_ROOT" "$UNIT"
  echo "unit will wait for $MEDIA_ROOT to be mounted"
fi

systemctl daemon-reload
systemctl enable cyberdeck.service

echo
echo "installed: $UNIT"
echo "  user:       $RUN_USER"
echo "  app:        $APPDIR"
echo "  media root: $MEDIA_ROOT"
echo
echo "start now:  sudo systemctl start cyberdeck"
echo "logs:       journalctl -u cyberdeck -f"
echo "stop/undo:  sudo systemctl disable --now cyberdeck && sudo systemctl enable --now getty@tty1"
