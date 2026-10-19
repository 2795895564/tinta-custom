#include "quickjump_indexer.h"

#include <cwctype>
#include <utility>

namespace {

// Kept in sync with isSupportedDocumentPath() in document.cpp. Duplicating the
// extension list here keeps the indexer linkable in the pure unit test (the
// document translation unit pulls in the parser and renderer).
bool quickJumpIsDocument(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot);
    for (wchar_t& c : ext) c = (wchar_t)std::towlower(c);
    return ext == L".md" || ext == L".markdown" || ext == L".mmd" ||
           ext == L".puml" || ext == L".plantuml" || ext == L".txt" ||
           ext == L".json" || ext == L".yaml" || ext == L".yml" ||
           ext == L".toml" || ext == L".ini" || ext == L".csv" ||
           ext == L".log";
}

}  // namespace

QuickJumpIndexer::~QuickJumpIndexer() { join(); }

bool QuickJumpIndexer::start(const std::wstring& root, uint64_t generation,
                             HWND notify) {
    if (active_.load()) return false;
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result_.reset();
    }
    active_ = true;
    worker_ = std::thread([this, root, generation, notify] {
        auto result = std::make_unique<QuickJumpResult>();
        result->root = root;
        result->generation = generation;
        workspace_index::Options opts;
        opts.maxEntries = 30000;
        opts.readTitles = true;
        workspace_index::scan(root, opts, quickJumpIsDocument, result->index);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_ = std::move(result);
        }
        active_ = false;
        if (notify) PostMessageW(notify, WM_APP_QUICKJUMP_READY, 0, 0);
    });
    return true;
}

bool QuickJumpIndexer::consume(QuickJumpResult& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!result_) return false;
    out = std::move(*result_);
    result_.reset();
    return true;
}

void QuickJumpIndexer::join() {
    if (worker_.joinable()) worker_.join();
    active_ = false;
}
