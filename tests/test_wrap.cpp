// Scratch harness for wrapText against the real VT323 face.
// Font::measure only needs TTF_OpenFont, so no window or GL context is required.
#include "render/Font.hpp"
#include "render/TextLayout.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <cstdio>
#include <string>

using namespace cyberdeck;

static int failures = 0;
static void check(bool cond, const std::string& what) {
    std::printf("%s  %s\n", cond ? "  ok  " : " FAIL ", what.c_str());
    if (!cond) ++failures;
}

int main() {
    if (TTF_Init() != 0) {
        std::printf("TTF_Init failed: %s\n", TTF_GetError());
        return 1;
    }
    Font font;
    if (!font.load("assets/fonts/VT323-Regular.ttf", 24)) {
        std::printf("could not load font\n");
        return 1;
    }

    const float w = 1104.0f;  // the answer panel's inner width

    // Empty / degenerate inputs must not crash or spin.
    check(wrapText(font, "", w).empty(), "empty text -> no lines");
    check(wrapText(font, "hello", 0.0f).empty(), "zero width -> no lines");
    check(wrapText(font, "hello", w).size() == 1, "short text -> 1 line");

    const std::string answer =
        "A transistor is a small semiconductor device that switches or amplifies "
        "electrical signals. It has three terminals, and a small current or voltage "
        "at one of them controls a much larger current between the other two. "
        "Transistors are the basic building block of every modern computer chip.";
    auto lines = wrapText(font, answer, w);
    check(lines.size() >= 2, "long answer wraps to " + std::to_string(lines.size()) + " lines");
    bool allFit = true;
    for (const auto& l : lines) {
        if (font.measure(l).x > w) { allFit = false; std::printf("      over: '%s'\n", l.c_str()); }
    }
    check(allFit, "every line fits the width");

    // No text is silently dropped: rejoining the lines restores the words.
    std::string rejoined;
    for (const auto& l : lines) { if (!rejoined.empty()) rejoined += " "; rejoined += l; }
    check(rejoined == answer, "wrapping is lossless");

    auto paras = wrapText(font, "first para\n\nsecond para", w);
    check(paras.size() == 3, "blank line preserved as an empty row, got " + std::to_string(paras.size()));

    // A single unbroken token far wider than the column.
    const std::string huge(400, 'M');
    auto hard = wrapText(font, huge, w);
    check(hard.size() > 1, "over-long word hard-splits into " + std::to_string(hard.size()));
    bool hardFit = true;
    for (const auto& l : hard) if (font.measure(l).x > w) hardFit = false;
    check(hardFit, "hard-split lines fit");
    std::string hardJoined;
    for (const auto& l : hard) hardJoined += l;
    check(hardJoined == huge, "hard split loses no characters");

    // Multi-byte text must not be cut mid-glyph.
    std::string uni;
    for (int i = 0; i < 300; ++i) uni += "café ";
    auto un = wrapText(font, uni, w);
    bool valid = true;
    for (const auto& l : un) {
        if (!l.empty() && (static_cast<unsigned char>(l.back()) & 0xC0) == 0xC0) valid = false;
        if (!l.empty() && (static_cast<unsigned char>(l.front()) & 0xC0) == 0x80) valid = false;
    }
    check(valid, "utf-8 boundaries respected across " + std::to_string(un.size()) + " lines");

    check(wrapText(font, answer, w, 2).size() == 2, "maxLines caps output");

    // Pathologically narrow column: must terminate, not hang.
    auto narrow = wrapText(font, "abcdefghij", 6.0f, 50);
    check(!narrow.empty(), "narrow width still produces lines (no infinite loop)");

    font.destroy();
    TTF_Quit();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
