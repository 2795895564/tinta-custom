#include "dirmodel.h"

#include <algorithm>
#include <cwctype>

#ifdef _WIN32
#include <windows.h>
#endif

namespace dirmodel {

static bool isSep(wchar_t c) { return c == L'\\' || c == L'/'; }

static std::wstring stripTrailing(std::wstring p) {
    while (p.size() > 1 && isSep(p.back()) && !(p.size() == 3 && p[1] == L':'))
        p.pop_back();
    return p;
}

bool isRoot(const std::wstring& path) {
    std::wstring p = stripTrailing(path);
    if (p.size() == 1 && isSep(p[0])) return true;
    if (p.size() == 2 && p[1] == L':') return true;
    if (p.size() == 3 && p[1] == L':' && isSep(p[2])) return true;
    return false;
}

std::wstring join(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    if (isSep(dir.back())) return dir + name;
    return dir + L'\\' + name;
}

std::wstring parentOf(const std::wstring& path) {
    std::wstring p = stripTrailing(path);
    if (isRoot(p)) return p;
    size_t cut = p.find_last_of(L"\\/");
    if (cut == std::wstring::npos) return p;
    std::wstring parent = p.substr(0, cut);
    if (parent.size() == 2 && parent[1] == L':') parent += L'\\';
    if (parent.empty()) parent = L"\\";
    return parent;
}

std::wstring leafOf(const std::wstring& path) {
    std::wstring p = stripTrailing(path);
    size_t cut = p.find_last_of(L"\\/");
    return cut == std::wstring::npos ? p : p.substr(cut + 1);
}

static std::wstring normKey(const std::wstring& path) {
    std::wstring p = stripTrailing(path);
    for (auto& c : p) c = c == L'/' ? L'\\' : (wchar_t)std::towlower(c);
    while (p.size() > 1 && p.back() == L'\\' && !(p.size() == 3 && p[1] == L':'))
        p.pop_back();
    return p;
}

bool isInside(const std::wstring& root, const std::wstring& path) {
    std::wstring r = normKey(root), p = normKey(path);
    if (r == p) return true;
    if (p.size() <= r.size() || p.compare(0, r.size(), r) != 0) return false;
    return r.back() == L'\\' || p[r.size()] == L'\\';
}

int naturalCompare(const std::wstring& a, const std::wstring& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::iswdigit(a[i]) && std::iswdigit(b[j])) {
            size_t si = i, sj = j;
            while (si < a.size() && a[si] == L'0') si++;
            while (sj < b.size() && b[sj] == L'0') sj++;
            size_t ei = si, ej = sj;
            while (ei < a.size() && std::iswdigit(a[ei])) ei++;
            while (ej < b.size() && std::iswdigit(b[ej])) ej++;
            if (ei - si != ej - sj) return (ei - si) < (ej - sj) ? -1 : 1;
            int c = a.compare(si, ei - si, b, sj, ej - sj);
            if (c != 0) return c < 0 ? -1 : 1;
            i = ei;
            j = ej;
            continue;
        }
        wchar_t x = (wchar_t)std::towlower(a[i]), y = (wchar_t)std::towlower(b[j]);
        if (x != y) return x < y ? -1 : 1;
        i++;
        j++;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

void sortEntries(std::vector<Entry>& entries, const Options& opts) {
    std::stable_sort(entries.begin(), entries.end(),
                     [&](const Entry& a, const Entry& b) {
        bool fa = a.kind == Kind::Folder, fb = b.kind == Kind::Folder;
        if (fa != fb) return fa;  // folders first, whatever the direction
        int c = 0;
        if (opts.key == SortKey::Modified)
            c = a.mtime == b.mtime ? 0 : (a.mtime < b.mtime ? -1 : 1);
        else if (opts.key == SortKey::Size && !fa)
            c = a.size == b.size ? 0 : (a.size < b.size ? -1 : 1);
        if (c != 0) return opts.descending ? c > 0 : c < 0;
        c = naturalCompare(a.name, b.name);
        if (opts.key == SortKey::Name && opts.descending) c = -c;
        return c < 0;
    });
}

int fuzzyScore(const std::wstring& name, const std::wstring& query) {
    if (query.empty()) return 0;
    std::wstring n = name, q = query;
    for (auto& c : n) c = (wchar_t)std::towlower(c);
    for (auto& c : q) c = (wchar_t)std::towlower(c);
    size_t pos = n.find(q);
    if (pos != std::wstring::npos)
        return 1000 - (int)pos * 4 - (int)(n.size() - q.size());
    size_t qi = 0, first = std::wstring::npos, last = 0;
    for (size_t i = 0; i < n.size() && qi < q.size(); i++) {
        if (n[i] == q[qi]) {
            if (first == std::wstring::npos) first = i;
            last = i;
            qi++;
        }
    }
    if (qi < q.size()) return -1;
    return 500 - (int)(last - first + 1) - (int)first;
}

std::vector<int> visible(const std::vector<Entry>& entries, const Options& opts,
                         const std::wstring& query) {
    struct R { int idx, score; };
    std::vector<R> hits;
    for (int i = 0; i < (int)entries.size(); i++) {
        const Entry& e = entries[i];
        if (e.hidden && !opts.showHidden) continue;
        if (e.kind == Kind::Other && !opts.showOther) continue;
        int s = fuzzyScore(e.name, query);
        if (s < 0) continue;
        hits.push_back({i, s});
    }
    if (!query.empty())
        std::stable_sort(hits.begin(), hits.end(),
                         [](const R& a, const R& b) { return a.score > b.score; });
    std::vector<int> out;
    out.reserve(hits.size());
    for (auto& h : hits) out.push_back(h.idx);
    return out;
}

std::wstring formatSize(uint64_t bytes) {
    if (bytes < 1024) return std::to_wstring(bytes) + L" B";
    double v = (double)bytes;
    const wchar_t* unit[] = {L"KB", L"MB", L"GB"};
    int u = -1;
    while (v >= 1024.0 && u < 2) { v /= 1024.0; u++; }
    wchar_t buf[32];
    int tenths = (int)(v * 10.0 + 0.5);
    if (v >= 100.0) tenths = (int)(v + 0.5) * 10;
    if (tenths % 10 == 0 || v >= 100.0)
        swprintf(buf, 32, L"%d %ls", tenths / 10, unit[u]);
    else
        swprintf(buf, 32, L"%d.%d %ls", tenths / 10, tenths % 10, unit[u]);
    return buf;
}

#ifdef _WIN32
bool scan(const std::wstring& dir, bool (*isDocument)(const std::wstring&),
          Listing& out) {
    out.path = dir;
    out.entries.clear();
    out.ok = false;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(join(dir, L"*").c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        Entry e;
        e.name = name;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.kind = isDir ? Kind::Folder
                       : (isDocument && isDocument(name) ? Kind::Document : Kind::Other);
        e.hidden = (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0 || name[0] == L'.';
        e.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        e.mtime = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) |
                  fd.ftLastWriteTime.dwLowDateTime;
        out.entries.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    out.ok = true;
    return true;
}

std::wstring readHead(const std::wstring& path, size_t maxBytes) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"";
    std::string buf(maxBytes, '\0');
    DWORD got = 0;
    ReadFile(f, buf.data(), (DWORD)maxBytes, &got, nullptr);
    CloseHandle(f);
    buf.resize(got);
    if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF) buf.erase(0, 3);  // BOM
    if (got == maxBytes) {  // cut at a line end, never inside a UTF-8 sequence
        size_t nl = buf.find_last_of('\n');
        if (nl != std::string::npos) buf.resize(nl);
    }
    if (buf.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)buf.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)buf.size(), w.data(), n);
    return w;
}
#endif

}  // namespace dirmodel
