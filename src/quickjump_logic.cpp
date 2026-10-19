#include "quickjump_logic.h"

#include <cwctype>

namespace quickjump_logic {

namespace {

std::wstring normalizedPath(const std::wstring& path) {
    std::wstring out = path;
    while (out.size() > 3 && (out.back() == L'\\' || out.back() == L'/'))
        out.pop_back();
    for (wchar_t& c : out) c = (wchar_t)std::towlower(c);
    return out;
}

}  // namespace

bool samePath(const std::wstring& a, const std::wstring& b) {
    if (a.empty() || b.empty()) return a.empty() && b.empty();
    return normalizedPath(a) == normalizedPath(b);
}

bool resultIsCurrent(uint64_t currentGeneration, const std::wstring& currentRoot,
                     uint64_t resultGeneration, const std::wstring& resultRoot) {
    return currentGeneration != 0 && currentGeneration == resultGeneration &&
           samePath(currentRoot, resultRoot);
}

int moveSelection(int selected, int count, int delta) {
    if (count <= 0) return 0;
    long long next = (long long)selected + delta;
    if (next < 0) next = 0;
    if (next > count - 1) next = count - 1;
    return (int)next;
}

EmptyState emptyState(bool hasHits, bool queryEmpty, bool scanning, bool ready,
                      bool hasRoot, unsigned long error) {
    if (!hasRoot) return EmptyState::NoRoot;
    if (error) return EmptyState::Error;
    if (hasHits) return EmptyState::None;
    if (scanning || !ready) return EmptyState::Indexing;
    return queryEmpty ? EmptyState::RecentEmpty : EmptyState::NoMatches;
}

}  // namespace quickjump_logic
