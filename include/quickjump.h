#pragma once
// P2b: Ctrl+K command palette over the workspace index.
#include <d2d1.h>
#include <windows.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "quickjump_indexer.h"
#include "workspace_index.h"

struct App;

struct QuickJumpState {
    bool open = false;
    std::wstring query;
    size_t caret = 0;
    std::vector<workspace_index::Hit> hits;
    int selected = 0;
    int topRow = 0;
    int visibleRows = 8;
    float animation = 0.0f;
    bool scanning = false;
    bool ready = false;
    unsigned long error = 0;
    std::wstring root;
    // Scan identity: bumped whenever the palette opens, so results of a scan
    // requested by an older palette session are recognizably stale. The
    // generation actually on the worker is scanGeneration (0 = queued behind
    // another scan).
    uint64_t generation = 0;
    uint64_t scanGeneration = 0;
    double indexedAt = 0.0;  // steady-clock seconds
    D2D1_RECT_F card{}, field{};
    float imeCaretX = 0.0f;  // client coords, set while rendering
    float imeCaretY = 0.0f;
    std::vector<std::pair<D2D1_RECT_F, int>> rowHits;
};

void quickJumpOpen(App& app, HWND hwnd);
void quickJumpClose(App& app, HWND hwnd);
void quickJumpIndexReady(App& app, HWND hwnd);
bool quickJumpKeyDown(App& app, HWND hwnd, WPARAM vk);  // true = consumed
bool quickJumpChar(App& app, HWND hwnd, wchar_t c);     // true = consumed
bool quickJumpMouseDown(App& app, HWND hwnd, int x, int y);
bool quickJumpMouseMove(App& app, HWND hwnd, int x, int y);
void quickJumpWheel(App& app, HWND hwnd, float notches);
void renderQuickJump(App& app);
// IME composition while the palette owns the query field. Return true when the
// message belongs to the palette.
bool quickJumpPositionImeWindow(App& app, HWND hwnd);
bool quickJumpImeComposition(App& app, HWND hwnd, LPARAM lParam);
