#pragma once
// P2a: repository-wide index for Quick Jump. The query/ranking side is pure;
// scan() is the only part that touches the filesystem.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace workspace_index {

struct Entry {
    std::wstring path;       // absolute path
    std::wstring name;       // file/folder name
    std::wstring relative;   // path below the repo root, backslash-separated
    std::wstring parent;     // parent below the repo root; empty at root
    std::wstring title;      // first Markdown H1, when readTitles is on
    uint64_t size = 0;
    uint64_t mtime = 0;      // FILETIME ticks, 0 = unknown
    bool folder = false;
};

struct Index {
    std::wstring root;
    std::vector<Entry> entries;
    bool truncated = false;  // maxEntries was reached
    unsigned long error = 0; // Win32 error when the root could not be opened
};

struct Options {
    bool includeHidden = false;
    bool includeFolders = true;
    bool readTitles = true;
    size_t maxEntries = 20000;
};

struct Hit {
    int index = -1;
    int score = 0;
};

// First ATX H1 in a Markdown head. Leading whitespace is allowed; "##" is not
// a document title. Returns empty when there is no H1.
std::wstring titleFromMarkdown(const std::wstring& text);

#ifdef _WIN32
// isDocument may be null, in which case every non-hidden file is indexed.
// Hidden files and hidden directories are skipped unless includeHidden is set.
// Reparse points (junctions/symlinks) are not followed.
bool scan(const std::wstring& root, const Options& opts,
          bool (*isDocument)(const std::wstring&), Index& out);
#endif

// Empty query returns the most recently modified entries. Multi-word queries
// are ANDed; each token may match the name, title, or relative path. Name
// matches outrank title and path matches; ties use mtime and natural order.
std::vector<Hit> query(const Index& index, const std::wstring& text,
                       size_t limit = 50);

}  // namespace workspace_index
