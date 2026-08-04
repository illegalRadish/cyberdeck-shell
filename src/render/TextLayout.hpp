#pragma once

#include <string>
#include <vector>

namespace cyberdeck {

class Font;

// Greedy word wrap to a pixel width. Splits on '\n' first so paragraphs are
// preserved, then on spaces. A single word wider than maxWidth is hard-split at
// a UTF-8 boundary rather than overflowing.
//
// Each measurement is a TTF_SizeUTF8 call, so this costs roughly one call per
// word: never call it from draw(). Wrap when the text changes and cache the
// result.
std::vector<std::string> wrapText(const Font& font, const std::string& text,
                                  float maxWidth, int maxLines = 400);

}  // namespace cyberdeck
