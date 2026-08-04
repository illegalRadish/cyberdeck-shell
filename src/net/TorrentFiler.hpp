#pragma once

#include "media/MediaTypes.hpp"

#include <string>

// Decides where a finished torrent belongs in PI LIB.
//
// Everything above destinationDir() is pure string logic with no filesystem
// access, which is what makes the heuristics unit-testable without a daemon,
// a network, or a scratch directory (see tests/test_torrent_filer.cpp).
namespace cyberdeck::torrentfiler {

struct EpisodeInfo {
    std::string show;   // "Some Show", separators cleaned
    int season = 0;
    bool valid = false;
};

// Recognises S01E03, 1x03, and bare "Season 2" pack names. The show name is
// whatever precedes the marker.
EpisodeInfo parseEpisode(const std::string& name);

// "Some.Show_Name" -> "Some Show Name". Dots and underscores become spaces,
// runs of whitespace collapse, and the result is trimmed.
std::string cleanTitle(const std::string& raw);

// "Movie.Name.2019.1080p.BluRay.x264" -> "Movie Name (2019)". Falls back to a
// tag-stripped title when there is no year.
std::string movieFolderName(const std::string& name);

// Route implied by the torrent's own name, before looking at any files.
MediaType routeFromName(const std::string& name);

// Refines nameRoute with what the completed payload actually contains, by
// total bytes per media type. Junk (.nfo, .txt, samples, anything under 1% of
// the largest file) is excluded. Falls back to nameRoute when the payload is
// unreadable or inconclusive.
MediaType routeFromPayload(const std::string& payloadPath, MediaType nameRoute);

// PI LIB subfolder for a route, or nullptr when the route has no home and the
// payload should stay in Downloads.
//
// These strings must keep matching the folder names mediaTypeFromRelativePath()
// tests in MediaTypes.cpp — that function is the only thing deciding how a file
// is classified once the scanner indexes it, so a mismatch here files media
// into a folder the library then labels "other".
const char* folderForRoute(MediaType route);

// Absolute directory to move the payload into, or empty when it should stay
// put. This is the *parent* the payload lands in: Transmission's
// torrent-set-location moves the torrent's top-level file or folder into it.
std::string destinationDir(const std::string& mediaRoot, MediaType route,
                           const std::string& name);

}  // namespace cyberdeck::torrentfiler
