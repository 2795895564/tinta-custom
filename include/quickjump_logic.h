#pragma once
// P2b: pure decisions for the Ctrl+K palette. No Win32, no D2D, no I/O.
#include <cstdint>
#include <string>

namespace quickjump_logic {

enum class EmptyState {
    None,         // rows are drawn
    NoRoot,       // no repository or document folder to index
    Error,        // the root could not be scanned
    Indexing,     // no rows yet and a scan is running (or queued)
    RecentEmpty,  // empty query, index is ready, nothing to list
    NoMatches,    // non-empty query, no hits
};

// Windows paths are case-insensitive and tolerate a trailing separator.
bool samePath(const std::wstring& a, const std::wstring& b);

// A worker result may only be installed when it belongs to the exact scan the
// palette asked for. Root equality alone would accept an old result after the
// user switched A -> B -> A while a scan was in flight.
bool resultIsCurrent(uint64_t currentGeneration, const std::wstring& currentRoot,
                     uint64_t resultGeneration, const std::wstring& resultRoot);

// Clamped list movement: delta may be +/-1 or a page; count 0 selects 0.
int moveSelection(int selected, int count, int delta);

// Decides which placeholder the list area shows (only called when no rows, but
// also useful in tests).
EmptyState emptyState(bool hasHits, bool queryEmpty, bool scanning, bool ready,
                      bool hasRoot, unsigned long error);

}  // namespace quickjump_logic
