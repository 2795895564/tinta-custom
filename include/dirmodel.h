#pragma once
// Pure directory model for the Home view. No App, no Direct2D: everything
// here except scan()/readHead() builds and tests on any platform.
#include <cstdint>
#include <string>
#include <vector>

namespace dirmodel {

enum class Kind { Folder, Document, Other };
enum class SortKey { Name, Modified, Size };

struct Entry {
    std::wstring name;
    Kind kind = Kind::Other;
    uint64_t size = 0;
    uint64_t mtime = 0;  // FILETIME ticks (100 ns since 1601); 0 = unknown
    bool hidden = false;
};

struct Options {
    bool showHidden = false;
    bool showOther = true;  // non-Markdown files, drawn dimmed, not openable
    SortKey key = SortKey::Name;
    bool descending = false;
};

struct Listing {
    std::wstring path;
    std::vector<Entry> entries;  // raw: hidden flagged, nothing filtered
    bool ok = false;
};

// Windows-style paths; both '\\' and '/' count as separators.
bool isRoot(const std::wstring& path);
std::wstring join(const std::wstring& dir, const std::wstring& name);
std::wstring parentOf(const std::wstring& path);
std::wstring leafOf(const std::wstring& path);
bool isInside(const std::wstring& root, const std::wstring& path);  // equal or below

// Case-insensitive, digit runs compare as numbers: 2.md < 10.md
int naturalCompare(const std::wstring& a, const std::wstring& b);
// Folders first always; then by key (descending flips the key, not the grouping)
void sortEntries(std::vector<Entry>& entries, const Options& opts);
// -1 = no match. Substring beats subsequence; earlier and tighter is better.
int fuzzyScore(const std::wstring& name, const std::wstring& query);
// Indices of entries to show: hidden/other filtering, then ranked by query.
std::vector<int> visible(const std::vector<Entry>& entries, const Options& opts,
                         const std::wstring& query);
std::wstring formatSize(uint64_t bytes);  // "3.1 KB"

#ifdef _WIN32
// isDocument decides Kind::Document (pass the app's isSupportedDocumentPath).
bool scan(const std::wstring& dir, bool (*isDocument)(const std::wstring&),
          Listing& out);
// First maxBytes of a UTF-8 file as wide text, cut at a line end.
std::wstring readHead(const std::wstring& path, size_t maxBytes);
#endif

}  // namespace dirmodel
