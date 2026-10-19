#include "workspace_index.h"

#include "dirmodel.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace workspace_index {

namespace {

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

std::wstring trimTrailing(std::wstring path) {
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();
    return path;
}

std::wstring relativeOf(const std::wstring& root, const std::wstring& path) {
    if (path.size() <= root.size()) return L"";
    size_t start = root.size();
    if (path[start] == L'\\' || path[start] == L'/') start++;
    return path.substr(start);
}

std::wstring parentRelative(const std::wstring& relative) {
    size_t cut = relative.find_last_of(L"\\/");
    return cut == std::wstring::npos ? L"" : relative.substr(0, cut);
}

bool hasPrefix(const std::wstring& text, const std::wstring& prefix) {
    return text.size() >= prefix.size() &&
           text.compare(0, prefix.size(), prefix) == 0;
}

int matchToken(const Entry& e, const std::wstring& token) {
    const std::wstring name = lower(e.name);
    const std::wstring title = lower(e.title);
    const std::wstring rel = lower(e.relative);

    int best = -1;
    int nameScore = dirmodel::fuzzyScore(name, token);
    if (nameScore >= 0) {
        best = nameScore * 4 + 600;
        if (name == token) best += 4000;
        else if (hasPrefix(name, token)) best += 1600;
        else if (name.find(token) != std::wstring::npos) best += 600;
    }
    if (!title.empty()) {
        int score = dirmodel::fuzzyScore(title, token);
        if (score >= 0) {
            int weighted = score * 2 + 250;
            if (title == token) weighted += 1800;
            else if (hasPrefix(title, token)) weighted += 900;
            else if (title.find(token) != std::wstring::npos) weighted += 300;
            best = std::max(best, weighted);
        }
    }
    int relScore = dirmodel::fuzzyScore(rel, token);
    if (relScore >= 0) {
        int weighted = relScore + 100;
        if (rel.find(token) != std::wstring::npos) weighted += 200;
        best = std::max(best, weighted);
    }
    if (best >= 0) best += e.folder ? 0 : 15;
    return best;
}

}  // namespace

std::wstring titleFromMarkdown(const std::wstring& text) {
    bool fenced = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        size_t first = 0;
        while (first < line.size() && (line[first] == L' ' || line[first] == L'\t'))
            first++;
        std::wstring trimmed = line.substr(first);
        if (trimmed.rfind(L"```", 0) == 0 || trimmed.rfind(L"~~~", 0) == 0) {
            fenced = !fenced;
        } else if (!fenced && trimmed.size() > 1 && trimmed[0] == L'#') {
            size_t level = 0;
            while (level < trimmed.size() && trimmed[level] == L'#') level++;
            if (level == 1 && level < trimmed.size() && trimmed[level] == L' ') {
                std::wstring title = trimmed.substr(level + 1);
                while (!title.empty() && (title.back() == L' ' || title.back() == L'\t'))
                    title.pop_back();
                // Drop an optional closing sequence: "# Title ###"
                size_t cut = title.find_last_not_of(L'#');
                if (cut != std::wstring::npos && cut + 1 < title.size() &&
                    title[cut] == L' ') {
                    title.erase(cut + 1);
                    while (!title.empty() && title.back() == L' ') title.pop_back();
                }
                if (!title.empty()) return title;
            }
        }
        if (end == text.size()) break;
        pos = end + 1;
    }
    return L"";
}

#ifdef _WIN32

namespace {

bool scanDir(const std::wstring& root, const std::wstring& dir, int depth,
             const Options& opts, bool (*isDocument)(const std::wstring&),
             Index& out) {
    if (depth > 32) return true;
    std::wstring pattern = dir;
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/')
        pattern.push_back(L'\\');
    pattern.push_back(L'*');

    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return depth != 0;  // subdirs may vanish
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const bool hidden = (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0 ||
                            (!name.empty() && name[0] == L'.');
        if (hidden && !opts.includeHidden) continue;
        const bool isFolder = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isFolder) {
            // Never follow junctions/symlinks: a repo can contain a loop.
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            if (name == L"node_modules") continue;
        }
        if (out.entries.size() >= opts.maxEntries) {
            out.truncated = true;
            break;
        }

        Entry e;
        e.path = dirmodel::join(dir, name);
        e.name = name;
        e.relative = relativeOf(root, e.path);
        e.parent = parentRelative(e.relative);
        e.folder = isFolder;
        e.mtime = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) |
                  fd.ftLastWriteTime.dwLowDateTime;
        e.size = isFolder ? 0 : (((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
        if (!isFolder) {
            if (isDocument && !isDocument(e.path)) continue;
            if (opts.readTitles && isDocument)
                e.title = titleFromMarkdown(dirmodel::readHead(e.path, 4096));
        }
        if (isFolder && !opts.includeFolders) {
            if (!scanDir(root, e.path, depth + 1, opts, isDocument, out)) break;
            continue;
        }
        out.entries.push_back(e);
        if (isFolder) {
            if (!scanDir(root, e.path, depth + 1, opts, isDocument, out)) break;
        }
        if (out.truncated) break;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return true;
}

}  // namespace

bool scan(const std::wstring& root, const Options& opts,
          bool (*isDocument)(const std::wstring&), Index& out) {
    out = Index{};
    out.root = trimTrailing(root);
    if (out.root.empty()) {
        out.error = ERROR_PATH_NOT_FOUND;
        return false;
    }
    out.entries.reserve(std::min<size_t>(opts.maxEntries, 4096));
    if (!scanDir(out.root, out.root, 0, opts, isDocument, out)) {
        out.error = GetLastError();
        return false;
    }
    return true;
}

#endif  // _WIN32

std::vector<Hit> query(const Index& index, const std::wstring& text, size_t limit) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    for (wchar_t c : text) {
        if (std::iswspace(c)) {
            if (!current.empty()) {
                tokens.push_back(lower(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) tokens.push_back(lower(current));

    std::vector<Hit> hits;
    hits.reserve(index.entries.size());
    for (int i = 0; i < (int)index.entries.size(); i++) {
        const Entry& e = index.entries[i];
        int score = 0;
        bool matched = true;
        for (const auto& token : tokens) {
            int s = matchToken(e, token);
            if (s < 0) {
                matched = false;
                break;
            }
            score += s;
        }
        if (matched) hits.push_back({i, score});
    }

    if (tokens.empty()) {
        std::stable_sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
            const Entry& ea = index.entries[a.index];
            const Entry& eb = index.entries[b.index];
            if (ea.mtime != eb.mtime) return ea.mtime > eb.mtime;
            return dirmodel::naturalCompare(ea.relative, eb.relative) < 0;
        });
    } else {
        std::stable_sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
            if (a.score != b.score) return a.score > b.score;
            const Entry& ea = index.entries[a.index];
            const Entry& eb = index.entries[b.index];
            if (ea.mtime != eb.mtime) return ea.mtime > eb.mtime;
            return dirmodel::naturalCompare(ea.relative, eb.relative) < 0;
        });
    }
    if (limit > 0 && hits.size() > limit) hits.resize(limit);
    return hits;
}

}  // namespace workspace_index
