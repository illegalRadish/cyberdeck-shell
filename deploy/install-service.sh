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

SEARCHED=""
if [ -z "$MEDIA_ROOT" ]; then
  for candidate in "/media/$RUN_USER/PI LIB" "/mnt/PI LIB" "/home/$RUN_USER/PI LIB" \
                   "/run/media/$RUN_USER/PI LIB" "$APPDIR/../PI LIB"; do
    SEARCHED="$SEARCHED\n    $candidate"
    if [ -d "$candidate" ]; then MEDIA_ROOT="$candidate"; break; fi
  done
elif [ ! -d "$MEDIA_ROOT" ]; then
  # An explicit path that does not exist is a typo or an unplugged drive, and
  # silently ignoring it would install a service pointing somewhere wrong.
  echo "warning: --media-root \"$MEDIA_ROOT\" is not a directory; ignoring it" >&2
  MEDIA_ROOT=""
fi

# A missing library is NOT fatal. The shell starts fine without one (the media
# screens just report it), MediaRoot re-runs discovery on every launch, and
# refusing to install the service here would leave the deck with no way to boot
# into anything at all until a USB drive turns up.
if [ -z "$MEDIA_ROOT" ]; then
  echo "PI LIB not found. Looked in:$(printf "$SEARCHED")" >&2
  echo "installing without a media root — the shell will search for PI LIB at each start." >&2
  echo "to pin one later:  sudo $0 --media-root \"/media/$RUN_USER/PI LIB\"" >&2
fi

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

if [ -z "$MEDIA_ROOT" ]; then
  # Leaving it set to an empty string would override discovery with nothing,
  # which is worse than not setting it at all.
  sed -i '/CYBERDECK_MEDIA_ROOT=/d' "$UNIT"
fi

# Wait for the media drive rather than racing it. Escaping the path is what
# systemd-escape is for; a hand-written unit with a space in it silently fails.
if [ -n "$MEDIA_ROOT" ] && mountpoint -q "$MEDIA_ROOT" 2>/dev/null; then
  sed -i "/^Wants=network-online.target/a RequiresMountsFor=$MEDIA_ROOT" "$UNIT"
  echo "unit will wait for $MEDIA_ROOT to be mounted"
fi

systemctl daemon-reload
systemctl enable cyberdeck.service

echo
echo "installed: $UNIT"
echo "  user:       $RUN_USER"
echo "  app:        $APPDIR"
echo "  media root: ${MEDIA_ROOT:-(auto-discovered at each start)}"
echo
echo "start now:  sudo systemctl start cyberdeck"
echo "logs:       journalctl -u cyberdeck -f"
echo "stop/undo:  sudo systemctl disable --now cyberdeck && sudo systemctl enable --now getty@tty1"
