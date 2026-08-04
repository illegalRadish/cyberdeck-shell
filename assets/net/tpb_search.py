#!/usr/bin/env python3
"""Torrent search for the cyberdeck shell, over The Pirate Bay's JSON backend.

Invoked on demand by TorrentSearchScreen and exits immediately.

PROTOCOL
    One flat JSON object per line on stdout, every value a string — the same
    contract as ask_deck.py and torrentctl.py. Never emit a dict or a list.

    kinds: result empty error

WHY apibay.org AND NOT THE HTML SITE
    apibay.org is the JSON backend the thepiratebay.org front end itself calls.
    Querying it directly means no HTML parsing and nothing that breaks the next
    time the page markup is restyled.

MAGNET CONSTRUCTION
    The API returns a bare info hash, not a magnet. A magnet built from the hash
    alone carries no trackers and relies entirely on DHT, which on a home
    connection behind NAT often finds no peers at all. The public trackers below
    are appended to every result — this is what the site's own front end does.

USAGE
    python3 -u tpb_search.py --query "debian iso" [--cat 0] [--limit 40]
"""

import argparse
import json
import sys
import urllib.error
import urllib.parse
import urllib.request

API_URL = "https://apibay.org/q.php"
TIMEOUT = 20

# apibay rejects urllib's default User-Agent, so one is always sent.
USER_AGENT = "Mozilla/5.0 (X11; Linux aarch64) cyberdeck-shell/1.0"

TRACKERS = [
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://open.stealth.si:80/announce",
    "udp://tracker.torrent.eu.org:451/announce",
    "udp://open.demonii.com:1337/announce",
    "udp://exodus.desync.com:6969/announce",
]

# TPB category -> cyberdeck media route. Sent with each result so the search
# screen can pre-set the destination and the filer usually needs no correction.
#
# These are exactly the strings mediaTypeFromString() accepts in MediaTypes.cpp
# ("tv", not "TvShow") so the C++ side parses them with the existing function
# instead of a second mapping that could drift out of sync.
CATEGORY_ROUTES = {
    1: "music",     # audio
    2: "movie",     # video, refined by the specific table below
    3: "other",     # applications
    4: "rom",       # games
    6: "other",     # other
}
SPECIFIC_ROUTES = {
    101: "music", 102: "book", 103: "other", 104: "music",
    201: "movie", 202: "movie", 207: "movie", 209: "movie",
    205: "tv", 208: "tv",
    601: "book", 602: "book",
}


def emit(**kw):
    """One flat JSON object per line. Values are always strings."""
    sys.stdout.write(json.dumps({k: str(v) for k, v in kw.items()}) + "\n")
    sys.stdout.flush()


def route_for(category):
    try:
        category = int(category)
    except (TypeError, ValueError):
        return "download"
    if category in SPECIFIC_ROUTES:
        return SPECIFIC_ROUTES[category]
    return CATEGORY_ROUTES.get(category // 100, "download")


def magnet_for(info_hash, name):
    parts = ["magnet:?xt=urn:btih:" + info_hash,
             "dn=" + urllib.parse.quote(name)]
    parts.extend("tr=" + urllib.parse.quote(t) for t in TRACKERS)
    return "&".join(parts)


def search(query, category, limit):
    url = API_URL + "?" + urllib.parse.urlencode({"q": query, "cat": category})
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
            payload = json.loads(response.read().decode("utf-8", "replace"))
    except urllib.error.HTTPError as exc:
        raise RuntimeError("search failed (http %d)" % exc.code)
    except urllib.error.URLError as exc:
        raise RuntimeError("search unreachable (%s)" % exc.reason)
    except (ValueError, OSError) as exc:
        raise RuntimeError("search returned junk (%s)" % exc)

    if not isinstance(payload, list):
        raise RuntimeError("unexpected response shape")

    # No-match is signalled by a single sentinel row with id "0" rather than an
    # empty array, so an unfiltered caller shows one fake result named
    # "No results returned".
    rows = [r for r in payload
            if isinstance(r, dict) and str(r.get("id", "0")) != "0"]
    if not rows:
        emit(kind="empty")
        return

    def seeders(row):
        try:
            return int(row.get("seeders", 0))
        except (TypeError, ValueError):
            return 0

    rows.sort(key=seeders, reverse=True)

    for row in rows[:limit]:
        info_hash = str(row.get("info_hash", "")).strip()
        name = str(row.get("name", "")).strip()
        if not info_hash or not name:
            continue
        emit(
            kind="result",
            name=name,
            hash=info_hash,
            seeders=row.get("seeders", 0),
            leechers=row.get("leechers", 0),
            sizeBytes=row.get("size", 0),
            category=row.get("category", 0),
            route=route_for(row.get("category", 0)),
            magnet=magnet_for(info_hash, name),
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--query", required=True)
    parser.add_argument("--cat", default="0",
                        help="0 all, 100 audio, 201 movies, 205 tv, 400 games")
    parser.add_argument("--limit", type=int, default=40)
    args = parser.parse_args()

    query = args.query.strip()
    if not query:
        emit(kind="empty")
        return 0

    try:
        search(query, args.cat, max(1, args.limit))
    except RuntimeError as exc:
        emit(kind="error", message=str(exc))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
