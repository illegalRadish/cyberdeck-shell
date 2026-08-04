// Scratch harness for the torrent filing heuristics: which media folder a
// finished torrent belongs in, and what it gets named once it lands there.
//
// Everything except the payload block is pure string logic, so this runs with
// no daemon, no network, and no PI LIB. The payload block builds a throwaway
// tree under the system temp dir and removes it again.
#include "net/TorrentFiler.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace cyberdeck;
namespace fs = std::filesystem;

static int failures = 0;

static void check(bool cond, const std::string& what) {
    std::printf("%s  %s\n", cond ? "  ok  " : " FAIL ", what.c_str());
    if (!cond) ++failures;
}

static void checkEq(const std::string& got, const std::string& want,
                    const std::string& what) {
    const bool ok = got == want;
    std::printf("%s  %s\n", ok ? "  ok  " : " FAIL ", what.c_str());
    if (!ok) {
        std::printf("        want: \"%s\"\n        got:  \"%s\"\n", want.c_str(), got.c_str());
        ++failures;
    }
}

static void checkRoute(const std::string& name, MediaType want) {
    const MediaType got = torrentfiler::routeFromName(name);
    const bool ok = got == want;
    std::printf("%s  route \"%s\" -> %s\n", ok ? "  ok  " : " FAIL ", name.c_str(),
                mediaTypeToString(got));
    if (!ok) {
        std::printf("        want: %s\n", mediaTypeToString(want));
        ++failures;
    }
}

static void writeFile(const fs::path& p, std::size_t bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    const std::string chunk(1024, 'x');
    for (std::size_t written = 0; written < bytes; written += chunk.size()) {
        out.write(chunk.data(), static_cast<std::streamsize>(
                                    std::min(chunk.size(), bytes - written)));
    }
}

int main() {
    std::printf("\n-- routeFromName --\n");

    // TV: the three episode-marker forms.
    checkRoute("Some.Show.S01E03.1080p.WEB-DL.x264-GROUP", MediaType::TvShow);
    checkRoute("Another Show s2e11 HDTV", MediaType::TvShow);
    checkRoute("Old.Series.1x03.DVDRip", MediaType::TvShow);
    checkRoute("Some Show Season 2 COMPLETE 720p", MediaType::TvShow);

    // Movies: year, release tags, or a bare video file.
    checkRoute("Movie.Name.2020.BluRay.x264", MediaType::Movie);
    checkRoute("Another Movie (2019) 1080p", MediaType::Movie);
    checkRoute("Some.Film.mkv", MediaType::Movie);

    // Music.
    checkRoute("Artist - Album (2019) [FLAC]", MediaType::Music);
    checkRoute("Band Discography 1990-2005", MediaType::Music);
    checkRoute("single track.mp3", MediaType::Music);

    // Books and ROMs by extension.
    checkRoute("Some Book.epub", MediaType::Book);
    checkRoute("Super Game (USA).smc", MediaType::Rom);

    // Nothing recognisable stays in Downloads rather than being guessed at.
    checkRoute("random.bin", MediaType::Download);
    checkRoute("assorted files", MediaType::Download);
    checkRoute("", MediaType::Download);

    // A resolution must not read as an episode marker.
    checkRoute("Nature.Doc.2018.1920x1080.mkv", MediaType::Movie);

    std::printf("\n-- parseEpisode --\n");
    {
        const auto ep = torrentfiler::parseEpisode("Some.Show.S01E03.1080p.WEB-DL");
        check(ep.valid, "S01E03 parses");
        checkEq(ep.show, "Some Show", "show name cleaned of dots");
        check(ep.season == 1, "season is 1");
    }
    {
        const auto ep = torrentfiler::parseEpisode("Another_Show_s12e07");
        check(ep.valid && ep.season == 12, "two-digit season");
        checkEq(ep.show, "Another Show", "underscores become spaces");
    }
    {
        const auto ep = torrentfiler::parseEpisode("Movie.Name.2020.BluRay");
        check(!ep.valid, "a film has no episode marker");
    }

    std::printf("\n-- movieFolderName --\n");
    checkEq(torrentfiler::movieFolderName("Movie.Name.2020.BluRay.x264-GRP"),
            "Movie Name (2020)", "year kept, tags dropped");
    checkEq(torrentfiler::movieFolderName("Another Movie 1080p"), "Another Movie",
            "no year, tags dropped");
    checkEq(torrentfiler::movieFolderName("Some.Film.mkv"), "Some Film",
            "extension stripped");

    std::printf("\n-- destinationDir --\n");
    {
        const std::string root = "/lib";
        checkEq(torrentfiler::destinationDir(root, MediaType::TvShow,
                                             "Some.Show.S01E03.1080p"),
                "/lib/TV Shows/Some Show/Season 01", "tv nests under show and season");
        checkEq(torrentfiler::destinationDir(root, MediaType::Movie,
                                             "Movie.Name.2020.BluRay"),
                "/lib/Movies/Movie Name (2020)", "movie folder carries the year");
        checkEq(torrentfiler::destinationDir(root, MediaType::Book, "Some Book.epub"),
                "/lib/Books", "books stay a flat collection");
        checkEq(torrentfiler::destinationDir(root, MediaType::Download, "random.bin"), "",
                "unroutable payloads stay put");
    }

    // The folder names must round-trip through the classifier the scanner uses,
    // or filed media gets indexed as the wrong type.
    std::printf("\n-- folder names match MediaTypes classification --\n");
    for (MediaType type : {MediaType::Music, MediaType::Movie, MediaType::TvShow,
                           MediaType::Photo, MediaType::Video, MediaType::Book,
                           MediaType::Rom}) {
        const char* folder = torrentfiler::folderForRoute(type);
        const std::string relative = std::string(folder) + "/thing.dat";
        check(mediaTypeFromRelativePath(relative) == type,
              std::string("\"") + folder + "/\" classifies as " + mediaTypeToString(type));
    }

    // Scene releases ship artwork and sidecars beside the media. Classifying by
    // folder alone made a YTS promo JPG show up as a playable movie.
    std::printf("\n-- artwork and sidecars are not playable media --\n");
    check(mediaTypeFromRelativePath(
              "Movies/Avengers (2018)/Avengers.2018.1080p.mp4") == MediaType::Movie,
          "the actual film is still a movie");
    check(mediaTypeFromRelativePath("Movies/Avengers (2018)/www.YTS.AM.jpg") ==
              MediaType::Other,
          "promo JPG beside a film is not a movie");
    check(mediaTypeFromRelativePath("Movies/Some Film/Some.Film.srt") == MediaType::Other,
          "subtitles are not a movie");
    check(mediaTypeFromRelativePath("Music/Album/cover.jpg") == MediaType::Other,
          "album art is not a track");
    check(mediaTypeFromRelativePath("Music/Album/01 track.flac") == MediaType::Music,
          "the actual track is still music");
    check(mediaTypeFromRelativePath("TV Shows/Show/Season 01/ep.mkv") == MediaType::TvShow,
          "episodes are still tv");
    // An extension the table does not know must not vanish from its category.
    check(mediaTypeFromRelativePath("Movies/Old Film/old.mpg") == MediaType::Movie,
          "unknown container keeps the folder's verdict");
    check(mediaTypeFromRelativePath("Photos/holiday.jpg") == MediaType::Photo,
          "photos still classify as photos");
    check(mediaTypeFromRelativePath("Downloads/anything.jpg") == MediaType::Download,
          "the staging area is exempt");

    std::printf("\n-- routeFromPayload --\n");
    {
        const fs::path base = fs::temp_directory_path() / "cyberdeck_filer_test";
        std::error_code ec;
        fs::remove_all(base, ec);

        // A film with the usual junk beside it: the .nfo and the small cover
        // must not outvote the one large video file.
        const fs::path movie = base / "Some.Film.2020";
        writeFile(movie / "film.mkv", 400 * 1024);
        writeFile(movie / "film.nfo", 2 * 1024);
        writeFile(movie / "cover.jpg", 1024);
        check(torrentfiler::routeFromPayload(movie.string(), MediaType::Download) ==
                  MediaType::Movie,
              "video payload routes to Movies despite junk");

        // An album: many comparable audio files.
        const fs::path album = base / "Artist - Album";
        for (int i = 1; i <= 4; ++i) {
            writeFile(album / (std::to_string(i) + " track.flac"), 60 * 1024);
        }
        writeFile(album / "folder.jpg", 3 * 1024);
        check(torrentfiler::routeFromPayload(album.string(), MediaType::Download) ==
                  MediaType::Music,
              "audio payload routes to Music");

        // A photo pack: every file is small, so the 1% floor must not eat them.
        const fs::path photos = base / "Photo Pack";
        for (int i = 1; i <= 5; ++i) {
            writeFile(photos / (std::to_string(i) + ".jpg"), 20 * 1024);
        }
        check(torrentfiler::routeFromPayload(photos.string(), MediaType::Download) ==
                  MediaType::Photo,
              "uniform photo payload survives the junk floor");

        // A name that already says "TV" is not second-guessed by extensions,
        // which can only ever report "video".
        const fs::path show = base / "Some.Show.S01E03";
        writeFile(show / "episode.mkv", 200 * 1024);
        check(torrentfiler::routeFromPayload(show.string(), MediaType::TvShow) ==
                  MediaType::TvShow,
              "an episode marker outranks the payload's extensions");

        // Nothing recognisable: keep the name's verdict rather than inventing one.
        const fs::path junk = base / "misc";
        writeFile(junk / "data.bin", 50 * 1024);
        check(torrentfiler::routeFromPayload(junk.string(), MediaType::Download) ==
                  MediaType::Download,
              "unrecognisable payload keeps the name route");

        check(torrentfiler::routeFromPayload("/nonexistent/path", MediaType::Movie) ==
                  MediaType::Movie,
              "a missing payload falls back to the name route");

        fs::remove_all(base, ec);
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
