#include "render/TextLayout.hpp"

#include "render/Font.hpp"

namespace cyberdeck {

namespace {

bool isUtf8Continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// Largest prefix of `word` that fits, split on a UTF-8 boundary so a multi-byte
// glyph is never cut in half. Always returns at least one character, otherwise
// an over-narrow column would loop forever.
std::size_t fittingPrefix(const Font& font, const std::string& word, float maxWidth) {
    std::size_t cut = word.size();
    while (cut > 1) {
        std::size_t candidate = cut - 1;
        while (candidate > 1 && isUtf8Continuation(word[candidate])) {
            --candidate;
        }
        if (font.measure(word.substr(0, candidate)).x <= maxWidth) {
            return candidate;
        }
        cut = candidate;
    }
    return 1;
}

void wrapParagraph(const Font& font, const std::string& paragraph, float maxWidth,
                   int maxLines, std::vector<std::string>& out) {
    if (paragraph.empty()) {
        out.push_back({});
        return;
    }

    std::string line;
    std::size_t i = 0;
    while (i < paragraph.size() && static_cast<int>(out.size()) < maxLines) {
        while (i < paragraph.size() && paragraph[i] == ' ') {
            ++i;
        }
        if (i >= paragraph.size()) {
            break;
        }
        const std::size_t wordEnd = paragraph.find(' ', i);
        std::string word = paragraph.substr(
            i, wordEnd == std::string::npos ? std::string::npos : wordEnd - i);
        i = (wordEnd == std::string::npos) ? paragraph.size() : wordEnd;

        const std::string candidate = line.empty() ? word : line + " " + word;
        if (font.measure(candidate).x <= maxWidth) {
            line = candidate;
            continue;
        }

        if (!line.empty()) {
            out.push_back(line);
            line.clear();
            if (static_cast<int>(out.size()) >= maxLines) {
                return;
            }
        }

        // The word alone still overflows: hard-split it across lines.
        while (font.measure(word).x > maxWidth &&
               static_cast<int>(out.size()) < maxLines) {
            const std::size_t cut = fittingPrefix(font, word, maxWidth);
            out.push_back(word.substr(0, cut));
            word.erase(0, cut);
        }
        line = word;
    }

    if (!line.empty() && static_cast<int>(out.size()) < maxLines) {
        out.push_back(line);
    }
}

}  // namespace

std::vector<std::string> wrapText(const Font& font, const std::string& text,
                                  float maxWidth, int maxLines) {
    std::vector<std::string> out;
    if (text.empty() || maxWidth <= 0.0f || maxLines <= 0 || !font.valid()) {
        return out;
    }

    std::size_t start = 0;
    while (start <= text.size() && static_cast<int>(out.size()) < maxLines) {
        std::size_t nl = text.find('\n', start);
        const std::size_t end = (nl == std::string::npos) ? text.size() : nl;
        std::string paragraph = text.substr(start, end - start);
        if (!paragraph.empty() && paragraph.back() == '\r') {
            paragraph.pop_back();
        }
        wrapParagraph(font, paragraph, maxWidth, maxLines, out);
        if (nl == std::string::npos) {
            break;
        }
        start = nl + 1;
    }
    return out;
}

}  // namespace cyberdeck
