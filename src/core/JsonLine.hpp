#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace cyberdeck::jsonline {

// Minimal reader for the one-line JSON objects ask_deck.py writes.
//
// The helper emits every value as a JSON string — numbers included
// ("pct":"37") — so only the string path is implemented here. Callers convert
// with toInt(). Nested objects and arrays are skipped, never parsed; the
// helper's emit() guarantees flat objects as a documented contract.
//
// Deliberately not a general JSON library: pulling one in for a single field
// lookup would cost more compile time on the Pi than this whole feature.

// True if the first non-space character is '{'. Anything else is a diagnostic
// line (a python traceback, a stray print) rather than protocol.
bool isObject(const std::string& line);

// Value for `key`, or nullopt when absent or the line is malformed.
std::optional<std::string> field(const std::string& line, std::string_view key);

// field() parsed as an integer, or `fallback` if missing/non-numeric.
int toInt(const std::optional<std::string>& value, int fallback = 0);

}  // namespace cyberdeck::jsonline
