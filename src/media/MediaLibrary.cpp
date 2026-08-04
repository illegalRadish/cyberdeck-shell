#include "media/MediaLibrary.hpp"

#include <iostream>
#include <sstream>

namespace cyberdeck {

bool MediaLibrary::initialize() {
    root_ = discoverMediaRoot();
    if (!root_.found) {
        ready_ = false;
        return false;
    }

    if (!db_.open(root_.dbPath)) {
        std::cerr << "MediaLibrary: failed to open database\n";
        ready_ = false;
        return false;
    }

    thumbs_ = std::make_unique<ThumbnailCache>(root_.thumbsDir);
    scanner_ = std::make_unique<MediaScanner>(db_, *thumbs_);
    ready_ = true;
    std::cout << "Media library ready at " << root_.path << '\n';
    return true;
}

void MediaLibrary::shutdown() {
    if (scanner_) {
        scanner_->requestStop();
        scanner_.reset();
    }
    thumbs_.reset();
    db_.close();
    ready_ = false;
}

void MediaLibrary::startScan() {
    if (!ready_ || !scanner_) {
        return;
    }
    scanner_->start(root_);
}

std::string MediaLibrary::statusLine() const {
    if (!root_.found) {
        return "PI LIB not found";
    }
    if (!ready_) {
        return "Library unavailable";
    }

    const ScanProgress p = scanProgress();
    std::ostringstream ss;
    if (p.running) {
        ss << "Indexing… " << p.filesSeen << " files";
        return ss.str();
    }

    ss << "PI LIB · " << db_.countAll() << " items";
    if (p.finished) {
        ss << " · indexed";
    }
    return ss.str();
}

}  // namespace cyberdeck
