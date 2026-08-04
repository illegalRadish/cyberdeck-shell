#!/usr/bin/env python3
"""Transmission RPC client for the cyberdeck shell.

Invoked on demand by TorrentManager and exits immediately; nothing here is a
daemon. Transmission itself is the daemon.

PROTOCOL
    One flat JSON object per line on stdout, every value a string — the same
    contract as ask_deck.py, read by the same minimal extractor in JsonLine.cpp.
    Never emit a dict or a list as a value.

    kinds: torrent added error empty ok

    Exit code is 0 whenever the RPC conversation succeeded, even if it returned
    nothing. A non-zero exit means the daemon could not be reached or answered
    with an error, and a `kind:error` line always accompanies it.

THE 409 HANDSHAKE (the part everyone gets wrong)
    Transmission answers the first request of a session with 409 Conflict and a
    X-Transmission-Session-Id header. The request must then be replayed with
    that header. This is not an error condition — it is the documented way the
    session id is obtained, and a client that treats 409 as a failure appears to
    work right up until the daemon is restarted underneath it.

USAGE
    python3 -u torrentctl.py --list
    python3 -u torrentctl.py --add "magnet:?xt=..." [--dest /path]
    python3 -u torrentctl.py --remove 3 [--delete-data]
    python3 -u torrentctl.py --pause 3
    python3 -u torrentctl.py --resume 3
    python3 -u torrentctl.py --set-location 3 --dest /path
"""

import argparse
import json
import sys
import urllib.error
import urllib.request

RPC_URL = "http://127.0.0.1:9091/transmission/rpc"
TIMEOUT = 15

# torrent-get is the only call whose field list matters; keep it minimal because
# the manager polls this every couple of seconds on a Pi.
FIELDS = [
    "id", "name", "hashString", "percentDone", "status", "rateDownload",
    "totalSize", "sizeWhenDone", "leftUntilDone", "downloadDir", "errorString",
]

# Transmission's status enum. Collapsed to lowercase words for the UI, which
# must keep its status strings low-cardinality (Font caches a texture per
# distinct string).
STATUS_NAMES = {
    0: "stopped", 1: "check-wait", 2: "checking", 3: "queued",
    4: "downloading", 5: "queued", 6: "seeding",
}

_session_id = ""


def emit(**kw):
    """One flat JSON object per line. Values are always strings."""
    sys.stdout.write(json.dumps({k: str(v) for k, v in kw.items()}) + "\n")
    sys.stdout.flush()


def rpc(method, arguments=None):
    """One RPC call, replaying once through the 409 session-id handshake.

    Raises RuntimeError with a one-line message suitable for display.
    """
    global _session_id
    body = json.dumps({"method": method, "arguments": arguments or {}}).encode()

    for attempt in (1, 2):
        request = urllib.request.Request(RPC_URL, data=body, method="POST")
        request.add_header("Content-Type", "application/json")
        if _session_id:
            request.add_header("X-Transmission-Session-Id", _session_id)
        try:
            with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
                payload = json.loads(response.read().decode("utf-8", "replace"))
        except urllib.error.HTTPError as exc:
            if exc.code == 409 and attempt == 1:
                # Expected on the first call of every session, and again
                # whenever the daemon restarts. Grab the id and replay.
                _session_id = exc.headers.get("X-Transmission-Session-Id", "")
                if not _session_id:
                    raise RuntimeError("daemon sent 409 without a session id")
                continue
            raise RuntimeError("rpc http %d" % exc.code)
        except urllib.error.URLError as exc:
            raise RuntimeError("torrent engine not running (%s)" % exc.reason)
        except (ValueError, OSError) as exc:
            raise RuntimeError("rpc failed (%s)" % exc)

        if payload.get("result") != "success":
            raise RuntimeError(str(payload.get("result", "rpc rejected")))
        return payload.get("arguments", {})

    raise RuntimeError("session handshake did not settle")


def ids_arg(value):
    """Transmission accepts numeric ids or 40-char hashes; pass both through."""
    try:
        return [int(value)]
    except ValueError:
        return [value]


def do_list():
    torrents = rpc("torrent-get", {"fields": FIELDS}).get("torrents", [])
    if not torrents:
        emit(kind="empty")
        return

    for t in torrents:
        size = t.get("sizeWhenDone") or t.get("totalSize") or 0
        left = t.get("leftUntilDone", 0)
        emit(
            kind="torrent",
            id=t.get("id", 0),
            name=t.get("name", ""),
            hash=t.get("hashString", ""),
            # Whole percent only: this string reaches the Font cache.
            percent=int(round(float(t.get("percentDone", 0.0)) * 100)),
            status=STATUS_NAMES.get(t.get("status", -1), "unknown"),
            rate=t.get("rateDownload", 0),
            sizeBytes=size,
            haveBytes=max(0, size - left),
            downloadDir=t.get("downloadDir", ""),
            error=t.get("errorString", ""),
        )


def do_add(magnet, dest):
    arguments = {"filename": magnet}
    if dest:
        arguments["download-dir"] = dest
    result = rpc("torrent-add", arguments)

    # A magnet already in the session comes back under "torrent-duplicate".
    # That is a success for our purposes — the user re-added something already
    # downloading — but it must be reported distinctly so the UI does not claim
    # to have started a second copy.
    added = result.get("torrent-added")
    duplicate = result.get("torrent-duplicate")
    entry = added or duplicate
    if not entry:
        raise RuntimeError("daemon accepted the magnet but returned no torrent")

    emit(
        kind="added",
        id=entry.get("id", 0),
        name=entry.get("name", ""),
        hash=entry.get("hashString", ""),
        duplicate="1" if duplicate else "0",
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--list", action="store_true")
    group.add_argument("--add", metavar="MAGNET")
    group.add_argument("--remove", metavar="ID")
    group.add_argument("--pause", metavar="ID")
    group.add_argument("--resume", metavar="ID")
    group.add_argument("--set-location", metavar="ID")
    parser.add_argument("--dest", default="")
    parser.add_argument("--delete-data", action="store_true")
    args = parser.parse_args()

    try:
        if args.list:
            do_list()
        elif args.add:
            do_add(args.add, args.dest)
        elif args.remove:
            # delete-local-data defaults off: by the time the filer removes a
            # torrent the payload has already been relocated into the media
            # library, and deleting it there would destroy what we just filed.
            rpc("torrent-remove", {"ids": ids_arg(args.remove),
                                   "delete-local-data": bool(args.delete_data)})
            emit(kind="ok")
        elif args.pause:
            rpc("torrent-stop", {"ids": ids_arg(args.pause)})
            emit(kind="ok")
        elif args.resume:
            rpc("torrent-start", {"ids": ids_arg(args.resume)})
            emit(kind="ok")
        elif args.set_location:
            if not args.dest:
                raise RuntimeError("--set-location needs --dest")
            # move=true makes Transmission relocate the payload itself and keep
            # seeding from the new path. Moving the files behind its back
            # instead leaves it reporting "No data found" and silently stops
            # the seed.
            rpc("torrent-set-location", {"ids": ids_arg(args.set_location),
                                         "location": args.dest, "move": True})
            emit(kind="ok")
    except RuntimeError as exc:
        emit(kind="error", message=str(exc))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
