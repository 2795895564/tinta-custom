#pragma once
// P2b: background workspace-index worker for Quick Jump.
//
// One scan runs at a time. Every request carries a generation chosen by the
// palette; the UI compares it (plus the root) before installing a result, so a
// repository switch can never overwrite the current list with an older scan.
#include "workspace_index.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

constexpr UINT WM_APP_QUICKJUMP_READY = WM_APP + 0x71;

struct QuickJumpResult {
    std::wstring root;
    uint64_t generation = 0;
    workspace_index::Index index;
};

class QuickJumpIndexer {
  public:
    ~QuickJumpIndexer();
    bool start(const std::wstring& root, uint64_t generation, HWND notify);
    bool consume(QuickJumpResult& out);
    bool running() const { return active_.load(); }
    void join();

  private:
    std::thread worker_;
    std::atomic<bool> active_{false};
    std::mutex mutex_;
    std::unique_ptr<QuickJumpResult> result_;
};
