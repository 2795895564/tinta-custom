#include "quickjump_indexer.h"
#include "quickjump_logic.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void writeFile(const fs::path& path, const char* text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

static void testPathsAndGenerations() {
    CHECK(quickjump_logic::samePath(L"C:\\Notes", L"c:\\notes\\"));
    CHECK(quickjump_logic::samePath(L"D:\\A\\B", L"d:\\a\\b"));
    CHECK(!quickjump_logic::samePath(L"C:\\Notes", L"C:\\Notes2"));
    CHECK(quickjump_logic::samePath(L"", L""));
    CHECK(!quickjump_logic::samePath(L"", L"C:\\Notes"));

    const std::wstring root = L"C:\\Notes";
    CHECK(quickjump_logic::resultIsCurrent(4, root, 4, root));
    CHECK(quickjump_logic::resultIsCurrent(4, root, 4, L"c:\\notes\\"));
    // Same root, older scan: still stale.
    CHECK(!quickjump_logic::resultIsCurrent(5, root, 4, root));
    // Different root: never install.
    CHECK(!quickjump_logic::resultIsCurrent(4, root, 4, L"D:\\Other"));
    CHECK(!quickjump_logic::resultIsCurrent(0, root, 0, root));
}

static void testMoveSelection() {
    CHECK(quickjump_logic::moveSelection(0, 0, 1) == 0);
    CHECK(quickjump_logic::moveSelection(3, 5, -1) == 2);
    CHECK(quickjump_logic::moveSelection(0, 5, -1) == 0);
    CHECK(quickjump_logic::moveSelection(4, 5, 1) == 4);
    CHECK(quickjump_logic::moveSelection(1, 5, 4) == 4);
    CHECK(quickjump_logic::moveSelection(4, 5, -40) == 0);
}

static void testEmptyStates() {
    using quickjump_logic::EmptyState;
    // No root wins over everything else: the palette explains what to open.
    CHECK(quickjump_logic::emptyState(false, true, false, false, false, 3) ==
          EmptyState::NoRoot);
    CHECK(quickjump_logic::emptyState(false, true, false, true, true, 5) ==
          EmptyState::Error);
    CHECK(quickjump_logic::emptyState(false, true, true, false, true, 0) ==
          EmptyState::Indexing);
    // Cached rows keep drawing while a refresh scan runs.
    CHECK(quickjump_logic::emptyState(true, true, true, true, true, 0) ==
          EmptyState::None);
    CHECK(quickjump_logic::emptyState(false, true, false, true, true, 0) ==
          EmptyState::RecentEmpty);
    CHECK(quickjump_logic::emptyState(false, false, false, true, true, 0) ==
          EmptyState::NoMatches);
    CHECK(quickjump_logic::emptyState(false, false, true, true, true, 0) ==
          EmptyState::Indexing);
}

static void testIndexerHandoff() {
    fs::path base = fs::current_path() /
                    (L"quickjump-indexer-test-" +
                     std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base / L"one" / L"sub");
    fs::create_directories(base / L"two");
    writeFile(base / L"one" / L"a.md", "# Alpha\n");
    writeFile(base / L"one" / L"sub" / L"b.txt", "Beta\n");
    writeFile(base / L"two" / L"c.md", "# Gamma\n");

    QuickJumpIndexer indexer;
    CHECK(indexer.start((base / L"one").wstring(), 7, nullptr));

    QuickJumpResult first;
    for (int i = 0; i < 1000 && !indexer.consume(first); i++) Sleep(5);
    CHECK(first.generation == 7);
    CHECK(quickjump_logic::samePath(first.root, (base / L"one").wstring()));
    CHECK(first.index.entries.size() == 3);  // sub, a.md, b.txt
    CHECK(first.index.error == 0);

    // A newer request gets a different generation even for the same folder.
    CHECK(indexer.start((base / L"two").wstring(), 8, nullptr));
    QuickJumpResult second;
    for (int i = 0; i < 1000 && !indexer.consume(second); i++) Sleep(5);
    CHECK(second.generation == 8);
    CHECK(quickjump_logic::samePath(second.root, (base / L"two").wstring()));
    CHECK(!quickjump_logic::resultIsCurrent(7, (base / L"one").wstring(), second.generation,
                                            second.root));

    fs::remove_all(base, ec);
}

int main() {
    testPathsAndGenerations();
    testMoveSelection();
    testEmptyStates();
    testIndexerHandoff();
    if (failures == 0) std::printf("quickjump tests passed\n");
    return failures ? 1 : 0;
}
