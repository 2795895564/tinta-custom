#include "workspace_index.h"

#include <windows.h>

#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static bool isDocument(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot);
    for (auto& c : ext) c = (wchar_t)std::towlower(c);
    return ext == L".md" || ext == L".txt";
}

static void writeFile(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

static const workspace_index::Entry* findEntry(
    const workspace_index::Index& index, const std::wstring& relative) {
    for (const auto& e : index.entries)
        if (e.relative == relative) return &e;
    return nullptr;
}

static void testTitles() {
    CHECK(workspace_index::titleFromMarkdown(L"# Hello\nbody") == L"Hello");
    CHECK(workspace_index::titleFromMarkdown(L"## Sub\n# Real\n") == L"Real");
    CHECK(workspace_index::titleFromMarkdown(L"  # Indented\r\n") == L"Indented");
    CHECK(workspace_index::titleFromMarkdown(L"```\n# Not\n```\n# Yes\n") == L"Yes");
    CHECK(workspace_index::titleFromMarkdown(L"# Title ###\n") == L"Title");
    CHECK(workspace_index::titleFromMarkdown(L"## Only sub\n").empty());
    CHECK(workspace_index::titleFromMarkdown(L"plain\n").empty());
}

static void testQuery() {
    workspace_index::Index index;
    index.root = L"C:\\notes";
    auto add = [&](const wchar_t* name, const wchar_t* relative,
                   const wchar_t* title, uint64_t mtime, bool folder) {
        workspace_index::Entry e;
        e.name = name;
        e.relative = relative;
        e.title = title;
        e.mtime = mtime;
        e.folder = folder;
        e.path = std::wstring(L"C:\\notes\\") + relative;
        index.entries.push_back(std::move(e));
    };
    add(L"alpha.md", L"docs\\alpha.md", L"Alpha Notes", 30, false);
    add(L"beta.md", L"beta.md", L"Beta", 10, false);
    add(L"sub", L"sub", L"", 20, true);
    add(L"alpha-old.md", L"archive\\alpha-old.md", L"", 5, false);

    auto hits = workspace_index::query(index, L"alpha");
    CHECK(hits.size() == 2);
    CHECK(index.entries[hits[0].index].name == L"alpha.md");
    CHECK(index.entries[hits[1].index].name == L"alpha-old.md");

    hits = workspace_index::query(index, L"alpha notes");
    CHECK(hits.size() == 1);
    CHECK(index.entries[hits[0].index].name == L"alpha.md");

    hits = workspace_index::query(index, L"notes");
    CHECK(hits.size() == 1);
    CHECK(index.entries[hits[0].index].name == L"alpha.md");

    hits = workspace_index::query(index, L"archive");
    CHECK(hits.size() == 1);
    CHECK(index.entries[hits[0].index].name == L"alpha-old.md");

    hits = workspace_index::query(index, L"beta");
    CHECK(hits.size() == 1);
    CHECK(index.entries[hits[0].index].name == L"beta.md");

    hits = workspace_index::query(index, L"");
    CHECK(hits.size() == 4);  // recent first
    CHECK(index.entries[hits[0].index].name == L"alpha.md");
    CHECK(index.entries[hits[1].index].name == L"sub");

    hits = workspace_index::query(index, L"", 2);
    CHECK(hits.size() == 2);

    hits = workspace_index::query(index, L"does-not-exist");
    CHECK(hits.empty());
}

static void testScan() {
    fs::path root = fs::current_path() /
                    (L"workspace-index-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    auto makeDir = [](const fs::path& p) {
        return CreateDirectoryW(p.c_str(), nullptr) != 0;
    };
    CHECK(makeDir(root));
    CHECK(makeDir(root / L".git"));
    CHECK(makeDir(root / L"node_modules"));
    CHECK(makeDir(root / L"sub"));
    CHECK(makeDir(root / L"sub" / L"nested"));
    CHECK(makeDir(root / L"empty"));

    writeFile(root / L"a.md", "# Alpha\nbody\n");
    writeFile(root / L"b.txt", "Beta plain\n");
    writeFile(root / L"z.md", "## Not a title\n");
    writeFile(root / L".hidden.md", "# Hidden\n");
    writeFile(root / L".git" / L"config", "# Git\n");
    writeFile(root / L"node_modules" / L"x.md", "# Node\n");
    writeFile(root / L"sub" / L"c.md", "# Gamma\n");
    writeFile(root / L"sub" / L"nested" / L"d.md", "# Delta\n");

    workspace_index::Options opts;
    opts.readTitles = true;
    opts.maxEntries = 100;
    workspace_index::Index index;
    CHECK(workspace_index::scan(root.wstring(), opts, isDocument, index));
    CHECK(index.error == 0);
    CHECK(!index.truncated);
    CHECK(index.entries.size() == 8);  // sub, empty, nested, a, b, z, c, d
    const auto* a = findEntry(index, L"a.md");
    const auto* z = findEntry(index, L"z.md");
    const auto* c = findEntry(index, L"sub\\c.md");
    const auto* empty = findEntry(index, L"empty");
    CHECK(a && a->title == L"Alpha");
    CHECK(z && z->title.empty());
    CHECK(c && c->parent == L"sub" && c->title == L"Gamma");
    CHECK(empty && empty->folder);
    CHECK(findEntry(index, L".hidden.md") == nullptr);
    CHECK(findEntry(index, L".git\\config") == nullptr);
    CHECK(findEntry(index, L"node_modules\\x.md") == nullptr);

    opts.includeFolders = false;
    workspace_index::Index filesOnly;
    CHECK(workspace_index::scan(root.wstring(), opts, isDocument, filesOnly));
    CHECK(filesOnly.entries.size() == 5);
    for (const auto& e : filesOnly.entries) CHECK(!e.folder);

    opts.includeFolders = true;
    opts.maxEntries = 2;
    workspace_index::Index limited;
    CHECK(workspace_index::scan(root.wstring(), opts, isDocument, limited));
    CHECK(limited.truncated);
    CHECK(limited.entries.size() == 2);

    opts.maxEntries = 100;
    opts.includeHidden = true;
    workspace_index::Index withHidden;
    CHECK(workspace_index::scan(root.wstring(), opts, isDocument, withHidden));
    CHECK(findEntry(withHidden, L".hidden.md") != nullptr);

    workspace_index::Index missing;
    CHECK(!workspace_index::scan((root / L"missing").wstring(), opts, isDocument, missing));
    CHECK(missing.error != 0);

    fs::remove_all(root, ec);
}

int main() {
    testTitles();
    testQuery();
    testScan();
    if (failures == 0) std::printf("workspace index tests passed\n");
    return failures ? 1 : 0;
}
