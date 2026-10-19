#include "quickjump.h"

#include "app.h"
#include "dirmodel.h"
#include "editor.h"
#include "homeview.h"
#include "i18n.h"
#include "input.h"
#include "quickjump_logic.h"
#include "tabs.h"
#include "utils.h"

#include <imm.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>

namespace {

constexpr size_t kMaxHits = 80;
constexpr double kIndexFreshSeconds = 5.0;

double nowSeconds() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool inRect(float x, float y, const D2D1_RECT_F& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

void refreshHits(App& app, bool resetSelection = false) {
    QuickJumpState& q = app.quickJump;
    q.hits.clear();
    if (q.root.empty() ||
        !quickjump_logic::samePath(app.quickJumpIndex.root, q.root))
        return;
    auto hits = workspace_index::query(app.quickJumpIndex, q.query, kMaxHits);
    for (auto hit : hits) {
        if (hit.index < 0 ||
            hit.index >= (int)app.quickJumpIndex.entries.size())
            continue;
        const auto& entry = app.quickJumpIndex.entries[hit.index];
        // Recents are files; folders are jump targets for an actual query.
        // Folder results also need a Home repository to navigate into.
        if (entry.folder && (q.query.empty() || app.home.root.empty()))
            continue;
        q.hits.push_back(hit);
    }
    if (resetSelection) {
        q.selected = 0;
        q.topRow = 0;
    }
    if (q.hits.empty()) q.selected = 0;
    else q.selected = std::clamp(q.selected, 0, (int)q.hits.size() - 1);
    q.topRow = std::max(0, std::min(q.topRow, (int)q.hits.size() - 1));
}

void ensureIndexer(App& app) {
    if (!app.quickJumpIndexer)
        app.quickJumpIndexer = std::make_unique<QuickJumpIndexer>();
}

// Requests a scan for the palette's current generation. Returns true when the
// worker accepted it; false means an older scan still owns the worker and the
// ready handler will retry (scanGeneration stays 0 until then).
bool startCurrentScan(App& app, HWND hwnd) {
    QuickJumpState& q = app.quickJump;
    if (q.root.empty()) return false;
    q.error = 0;  // a retry shows "Indexing", not the previous failure
    ensureIndexer(app);
    if (q.scanning && q.scanGeneration == q.generation &&
        app.quickJumpIndexer->running())
        return true;
    q.scanning = true;
    if (app.quickJumpIndexer->start(q.root, q.generation, hwnd)) {
        q.scanGeneration = q.generation;
        return true;
    }
    q.scanGeneration = 0;
    return false;
}

void activateSelection(App& app, HWND hwnd) {
    QuickJumpState& q = app.quickJump;
    if (q.selected < 0 || q.selected >= (int)q.hits.size()) return;
    const int entryIndex = q.hits[q.selected].index;
    if (entryIndex < 0 || entryIndex >= (int)app.quickJumpIndex.entries.size())
        return;
    const workspace_index::Entry entry =
        app.quickJumpIndex.entries[entryIndex];

    if (entry.folder) {
        // Folder jump: navigate the fixed Home tab, then make it the base.
        if (app.home.root.empty()) return;
        // Jumping into a folder means the Files screen, even from the picker.
        app.home.screen = HomeScreen::Home;
        homeNavigate(app.home, entry.path, L"");
        tabsEnsureHome(app);
        for (int i = 0; i < (int)app.tabs.size(); i++) {
            if (app.tabs[i].home) {
                tabActivate(app, hwnd, i);
                break;
            }
        }
        homeRefresh(app, hwnd);
    } else {
        // Open or activate the file as a tab in this window, leaving every
        // other tab's buffer untouched.
        tabOpenPath(app, hwnd, toUtf8(entry.path), true);
    }
    app.swallowCharsUntil = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(180);
    quickJumpClose(app, hwnd);
}

}  // namespace

void quickJumpOpen(App& app, HWND hwnd) {
    QuickJumpState& q = app.quickJump;
    if (q.open) {
        quickJumpClose(app, hwnd);
        return;
    }
    tabHoverCardDismiss(app, hwnd);
    q.open = true;
    q.query.clear();
    q.caret = 0;
    q.selected = 0;
    q.topRow = 0;
    q.animation = 0.0f;
    q.scanning = false;
    q.ready = false;
    q.error = 0;
    q.hits.clear();
    // Invalidate any scan requested by an earlier palette session.
    ++q.generation;
    q.scanGeneration = 0;

    q.root = app.home.root;
    if (q.root.empty() && !app.currentFile.empty())
        q.root = dirmodel::parentOf(toWide(app.currentFile));

    // The K that opened the palette must not become the first query character.
    app.swallowCharsUntil = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(180);
    if (q.root.empty()) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    ensureIndexer(app);
    const bool cached = !app.quickJumpIndex.root.empty() &&
                        quickjump_logic::samePath(app.quickJumpIndex.root, q.root);
    if (cached) {
        q.ready = true;
        q.error = app.quickJumpIndex.error;
        refreshHits(app);
    }
    // A fresh cache renders immediately; an old or missing one is rebuilt in
    // the background while those rows stay usable.
    if (!cached || nowSeconds() - q.indexedAt > kIndexFreshSeconds)
        startCurrentScan(app, hwnd);

    InvalidateRect(hwnd, nullptr, FALSE);
}

void quickJumpClose(App& app, HWND hwnd) {
    QuickJumpState& q = app.quickJump;
    // An unfinished IME composition belongs to the query field; cancel it so
    // its commit cannot land in the editor underneath.
    if (HIMC himc = ImmGetContext(hwnd)) {
        ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        ImmReleaseContext(hwnd, himc);
    }
    q.open = false;
    q.query.clear();
    q.caret = 0;
    q.hits.clear();
    q.selected = 0;
    q.topRow = 0;
    q.animation = 0.0f;
    q.scanning = false;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void quickJumpIndexReady(App& app, HWND hwnd) {
    QuickJumpState& q = app.quickJump;
    if (!app.quickJumpIndexer) return;
    QuickJumpResult result;
    if (!app.quickJumpIndexer->consume(result)) return;

    const bool current = quickjump_logic::resultIsCurrent(
        q.generation, q.root, result.generation, result.root);
    if (current) {
        app.quickJumpIndex = std::move(result.index);
        q.ready = true;
        q.scanning = false;
        q.scanGeneration = 0;
        q.error = app.quickJumpIndex.error;
        q.indexedAt = nowSeconds();
        if (q.open) refreshHits(app);
    }
    // A stale result must never replace the list; if it was the scan we were
    // waiting for, start the current one now that the worker is free.
    if (q.open && !q.root.empty()) {
        const bool haveCurrent =
            q.ready && quickjump_logic::samePath(app.quickJumpIndex.root, q.root);
        if (!haveCurrent && q.scanGeneration != q.generation)
            startCurrentScan(app, hwnd);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool quickJumpKeyDown(App& app, HWND hwnd, WPARAM vk) {
    QuickJumpState& q = app.quickJump;
    if (!q.open) return false;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (vk == VK_ESCAPE || (ctrl && vk == 'K')) {
        quickJumpClose(app, hwnd);
        return true;
    }
    if (vk == VK_RETURN) {
        activateSelection(app, hwnd);
        return true;
    }
    if (vk == VK_F5) {
        // Retry after an index error without closing the palette.
        startCurrentScan(app, hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT ||
        (ctrl && (vk == VK_HOME || vk == VK_END))) {
        const int page = std::max(1, q.visibleRows - 1);
        if (vk == VK_HOME) q.selected = 0;
        else if (vk == VK_END) q.selected = (int)q.hits.size() - 1;
        else if (vk == VK_UP) q.selected = quickjump_logic::moveSelection(q.selected, (int)q.hits.size(), -1);
        else if (vk == VK_DOWN) q.selected = quickjump_logic::moveSelection(q.selected, (int)q.hits.size(), 1);
        else if (vk == VK_PRIOR) q.selected = quickjump_logic::moveSelection(q.selected, (int)q.hits.size(), -page);
        else q.selected = quickjump_logic::moveSelection(q.selected, (int)q.hits.size(), page);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (ctrl && vk == 'V') {
        std::wstring pasted = clipboardLine(hwnd);
        if (!pasted.empty()) {
            const int at = std::clamp((int)q.caret, 0, (int)q.query.size());
            q.query.insert(q.query.begin() + at, pasted.begin(), pasted.end());
            q.caret = (size_t)at + pasted.size();
            refreshHits(app, true);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return true;
    }
    if (vk == VK_BACK || vk == VK_DELETE || vk == VK_LEFT || vk == VK_RIGHT ||
        vk == VK_HOME || vk == VK_END) {
        const int length = (int)q.query.size();
        bool changed = false;
        if (vk == VK_BACK) {
            if (q.caret > 0) {
                q.query.erase(q.caret - 1, 1);
                q.caret--;
                changed = true;
            }
        } else if (vk == VK_DELETE) {
            if (q.caret < length) {
                q.query.erase(q.caret, 1);
                changed = true;
            }
        } else if (vk == VK_LEFT) {
            if (q.caret > 0) q.caret--;
        } else if (vk == VK_RIGHT) {
            if (q.caret < length) q.caret++;
        } else if (vk == VK_HOME) {
            q.caret = 0;
        } else {
            q.caret = length;
        }
        if (changed) refreshHits(app, true);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    // Printable keys fall through to WM_CHAR (including OEM punctuation);
    // everything else is swallowed by the palette.
    return false;
}

bool quickJumpPositionImeWindow(App& app, HWND hwnd) {
    QuickJumpState& q = app.quickJump;
    if (!q.open) return false;
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) return true;  // the palette still owns the message
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = {(LONG)q.imeCaretX, (LONG)q.imeCaretY};
    ImmSetCompositionWindow(himc, &composition);
    CANDIDATEFORM candidate{};
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = composition.ptCurrentPos;
    ImmSetCandidateWindow(himc, &candidate);
    ImmReleaseContext(hwnd, himc);
    return true;
}

bool quickJumpImeComposition(App& app, HWND hwnd, LPARAM lParam) {
    QuickJumpState& q = app.quickJump;
    if (!q.open) return false;
    quickJumpPositionImeWindow(app, hwnd);
    if (!(lParam & GCS_RESULTSTR)) return true;  // composition preview only
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) return true;
    LONG bytes = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
    if (bytes > 0) {
        std::wstring text((size_t)bytes / sizeof(wchar_t), L'\0');
        LONG got =
            ImmGetCompositionStringW(himc, GCS_RESULTSTR, text.data(), bytes);
        if (got > 0) {
            int at = std::clamp((int)q.caret, 0, (int)q.query.size());
            for (wchar_t c : text) {
                if (c < 32 || q.query.size() >= 512) continue;
                q.query.insert(q.query.begin() + at, c);
                at++;
            }
            q.caret = (size_t)at;
            refreshHits(app, true);
        }
    }
    ImmReleaseContext(hwnd, himc);
    // The committed result replaces this keystroke's WM_CHAR stream.
    app.swallowCharsUntil = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(150);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool quickJumpChar(App& app, HWND hwnd, wchar_t c) {
    QuickJumpState& q = app.quickJump;
    if (!q.open) return false;
    if (c < 32) return true;
    if (q.query.size() >= 512) return true;
    const int at = std::clamp((int)q.caret, 0, (int)q.query.size());
    q.query.insert(q.query.begin() + at, c);
    q.caret = (size_t)at + 1;
    refreshHits(app, true);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool quickJumpMouseDown(App& app, HWND hwnd, int x, int y) {
    QuickJumpState& q = app.quickJump;
    if (!q.open) return false;
    for (const auto& hit : q.rowHits) {
        if (inRect((float)x, (float)y, hit.first)) {
            q.selected = hit.second;
            activateSelection(app, hwnd);
            app.swallowNextMouseUp = true;
            return true;
        }
    }
    if (!inRect((float)x, (float)y, q.card)) {
        quickJumpClose(app, hwnd);
        app.swallowNextMouseUp = true;
        return true;
    }
    app.swallowNextMouseUp = true;
    return true;
}

bool quickJumpMouseMove(App& app, HWND hwnd, int x, int y) {
    QuickJumpState& q = app.quickJump;
    if (!q.open) return false;
    for (const auto& hit : q.rowHits) {
        if (inRect((float)x, (float)y, hit.first)) {
            if (q.selected != hit.second) {
                q.selected = hit.second;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return true;
        }
    }
    return true;
}

void quickJumpWheel(App& app, HWND hwnd, float notches) {
    QuickJumpState& q = app.quickJump;
    if (!q.open) return;
    const int rows = std::max(1, q.visibleRows);
    const int maxTop = std::max(0, (int)q.hits.size() - rows);
    q.topRow -= (int)std::lround(notches * 3.0f);
    q.topRow = std::max(0, std::min(q.topRow, maxTop));
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- rendering -------------------------------------------------------------

namespace {

D2D1_COLOR_F mixColor(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                        a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

D2D1_COLOR_F alpha(const D2D1_COLOR_F& c, float a) {
    return D2D1::ColorF(c.r, c.g, c.b, a);
}

IDWriteTextFormat* makeFormat(App& app, float size, DWRITE_FONT_WEIGHT weight,
                              const wchar_t* face, bool wrap = false,
                              bool trim = true) {
    IDWriteTextFormat* fmt = nullptr;
    if (!app.dwriteFactory) return nullptr;
    if (FAILED(app.dwriteFactory->CreateTextFormat(
            face, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, dpi(app, size), L"", &fmt))) {
        return nullptr;
    }
    fmt->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP
                              : DWRITE_WORD_WRAPPING_NO_WRAP);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    // Result names and paths must never spill past the card. DirectWrite's
    // no-wrap layout otherwise draws glyphs outside the layout width.
    if (!wrap && trim) {
        IDWriteInlineObject* ellipsis = nullptr;
        if (SUCCEEDED(app.dwriteFactory->CreateEllipsisTrimmingSign(fmt,
                                                                   &ellipsis)) &&
            ellipsis) {
            DWRITE_TRIMMING trimming{};
            trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            fmt->SetTrimming(&trimming, ellipsis);
            ellipsis->Release();
        }
    }
    return fmt;
}

void drawText(App& app, const std::wstring& text, IDWriteTextFormat* fmt,
              const D2D1_RECT_F& rect, const D2D1_COLOR_F& color) {
    if (!fmt || text.empty()) return;
    app.brush->SetColor(color);
    app.renderTarget->DrawTextW(text.c_str(), (UINT32)text.size(), fmt, rect,
                                app.brush);
}

void fillRound(App& app, const D2D1_RECT_F& r, float radius,
               const D2D1_COLOR_F& color) {
    app.brush->SetColor(color);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(r, radius, radius), app.brush);
}

void strokeRound(App& app, const D2D1_RECT_F& r, float radius,
                 const D2D1_COLOR_F& color, float width) {
    app.brush->SetColor(color);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(r, radius, radius), app.brush, width);
}

std::wstring hitCountLabel(App& app, size_t count) {
    wchar_t text[64];
    swprintf_s(text, _countof(text),
               tr(app, count == 1 ? "quickjump.count_one" : "quickjump.count"),
               (unsigned)count);
    return text;
}

}  // namespace

void renderQuickJump(App& app) {
    QuickJumpState& q = app.quickJump;
    if (!q.open) return;
    const D2DTheme& th = app.theme;
    if (q.animation < 1.0f) {
        q.animation = std::min(1.0f, q.animation + 0.25f);
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    const float anim = q.animation;
    const float W = (float)app.width;
    const float H = (float)app.height;

    IDWriteTextFormat* titleFmt =
        makeFormat(app, 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, L"Segoe UI");
    IDWriteTextFormat* bodyFmt =
        makeFormat(app, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, L"Segoe UI", false,
                   false);
    IDWriteTextFormat* messageFmt =
        makeFormat(app, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, L"Segoe UI", true);
    IDWriteTextFormat* pathFmt =
        makeFormat(app, 10.5f, DWRITE_FONT_WEIGHT_NORMAL, L"Segoe UI");
    IDWriteTextFormat* badgeFmt =
        makeFormat(app, 8.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, L"Cascadia Mono");

    const float chrome = chromeTopHeight(app);
    const float cardW = std::min(dpi(app, 640.0f), W - dpi(app, 48.0f));
    const float cardH = std::min(dpi(app, 470.0f), H - chrome - dpi(app, 56.0f));
    const float cardX = (W - cardW) / 2.0f;
    const float cardY = chrome + std::max(dpi(app, 28.0f),
                                         (H - chrome - cardH) / 2.0f);
    q.card = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);

    app.brush->SetColor(D2D1::ColorF(0, 0, 0, 0.32f * anim));
    app.renderTarget->FillRectangle(D2D1::RectF(0, chrome, W, H), app.brush);
    fillRound(app, q.card, dpi(app, 12.0f),
              mixColor(th.background, th.text, th.isDark ? 0.06f : -0.02f));
    strokeRound(app, q.card, dpi(app, 12.0f), alpha(th.accent, 0.42f), 1.0f);

    const float pad = dpi(app, 18.0f);
    const float fieldY = cardY + pad;
    const float fieldH = dpi(app, 42.0f);
    q.field = D2D1::RectF(cardX + pad, fieldY, cardX + cardW - pad,
                          fieldY + fieldH);
    fillRound(app, q.field, dpi(app, 7.0f),
              mixColor(th.background, th.text, th.isDark ? 0.05f : -0.03f));
    strokeRound(app, q.field, dpi(app, 7.0f), alpha(th.accent, 0.55f), 1.0f);

    // Simple magnifier.
    const float ix = q.field.left + dpi(app, 16.0f);
    const float iy = (q.field.top + q.field.bottom) / 2.0f - dpi(app, 2.0f);
    app.brush->SetColor(alpha(th.text, 0.45f));
    app.renderTarget->DrawEllipse(
        D2D1::Ellipse(D2D1::Point2F(ix, iy), dpi(app, 5.0f), dpi(app, 5.0f)),
        app.brush, dpi(app, 1.4f));
    app.renderTarget->DrawLine(
        D2D1::Point2F(ix + dpi(app, 4.0f), iy + dpi(app, 4.0f)),
        D2D1::Point2F(ix + dpi(app, 8.0f), iy + dpi(app, 8.0f)), app.brush,
        dpi(app, 1.4f));

    const float textX = q.field.left + dpi(app, 32.0f);
    const int caret = std::clamp((int)q.caret, 0, (int)q.query.size());
    const std::wstring before = q.query.substr(0, caret);
    const float caretX = textX + measureText(app, before, bodyFmt);
    q.imeCaretX = caretX;
    q.imeCaretY = (q.field.top + q.field.bottom) / 2.0f;
    // Long queries scroll off the field instead of painting over the card.
    app.renderTarget->PushAxisAlignedClip(
        D2D1::RectF(textX, q.field.top, q.field.right - dpi(app, 8.0f),
                    q.field.bottom),
        D2D1_ANTIALIAS_MODE_ALIASED);
    if (q.query.empty()) {
        drawText(app, tr(app, "quickjump.placeholder"), bodyFmt,
                 D2D1::RectF(textX, q.field.top, q.field.right - dpi(app, 12.0f),
                             q.field.bottom),
                 alpha(th.text, 0.36f));
    } else {
        drawText(app, q.query, bodyFmt,
                 D2D1::RectF(textX, q.field.top, q.field.right - dpi(app, 12.0f),
                             q.field.bottom),
                 th.text);
    }
    app.brush->SetColor(th.accent);
    app.renderTarget->FillRectangle(
        D2D1::RectF(caretX, q.field.top + dpi(app, 9.0f),
                    caretX + dpi(app, 1.5f), q.field.bottom - dpi(app, 9.0f)),
        app.brush);
    app.renderTarget->PopAxisAlignedClip();

    const float listTop = q.field.bottom + dpi(app, 8.0f);
    const float footerH = dpi(app, 28.0f);
    const float listBottom = cardY + cardH - footerH;
    const float rowH = dpi(app, 48.0f);
    const auto state = quickjump_logic::emptyState(
        !q.hits.empty(), q.query.empty(), q.scanning, q.ready, !q.root.empty(),
        q.error);
    // The section label only belongs to a usable recent list; error/indexing
    // placeholders stand on their own.
    const bool showHeader =
        q.query.empty() &&
        (state == quickjump_logic::EmptyState::None ||
         state == quickjump_logic::EmptyState::RecentEmpty);
    const float headerH = showHeader ? dpi(app, 20.0f) : 0.0f;
    const float rowsTop = listTop + headerH;
    const int visible =
        std::max(1, (int)((listBottom - rowsTop) / rowH));
    q.visibleRows = visible;
    if (q.selected < q.topRow) q.topRow = q.selected;
    if (q.selected >= q.topRow + visible) q.topRow = q.selected - visible + 1;
    q.topRow = std::max(0, std::min(q.topRow, (int)q.hits.size() - 1));

    if (showHeader) {
        drawText(app, tr(app, "quickjump.recent"), badgeFmt,
                 D2D1::RectF(cardX + pad, listTop, cardX + cardW - pad,
                             listTop + headerH),
                 alpha(th.text, 0.38f));
    }

    q.rowHits.clear();
    if (state != quickjump_logic::EmptyState::None) {
        std::wstring message;
        D2D1_COLOR_F color = alpha(th.text, 0.52f);
        switch (state) {
            case quickjump_logic::EmptyState::NoRoot:
                message = tr(app, "quickjump.no_root");
                break;
            case quickjump_logic::EmptyState::Error: {
                wchar_t text[512];
                swprintf_s(text, _countof(text), tr(app, "quickjump.error"),
                           (unsigned)q.error);
                message = text;
                color = D2D1::ColorF(0.88f, 0.46f, 0.40f);
                break;
            }
            case quickjump_logic::EmptyState::Indexing:
                message = tr(app, "quickjump.indexing");
                break;
            case quickjump_logic::EmptyState::RecentEmpty:
                message = tr(app, "quickjump.recent_empty");
                break;
            case quickjump_logic::EmptyState::NoMatches:
                message = tr(app, "quickjump.no_matches");
                break;
            case quickjump_logic::EmptyState::None:
                break;
        }
        drawText(app, message, messageFmt,
                 D2D1::RectF(cardX + pad, rowsTop, cardX + cardW - pad,
                             rowsTop + rowH * 2),
                 color);
        if (state == quickjump_logic::EmptyState::Error) {
            drawText(app, q.root, pathFmt,
                     D2D1::RectF(cardX + pad, rowsTop + rowH * 2,
                                 cardX + cardW - pad, rowsTop + rowH * 2 + dpi(app, 22.0f)),
                     alpha(th.text, 0.40f));
        }
    }

    for (int row = 0; row < visible; row++) {
        const int hitIndex = q.topRow + row;
        if (hitIndex < 0 || hitIndex >= (int)q.hits.size()) break;
        const auto& hit = q.hits[hitIndex];
        if (hit.index < 0 || hit.index >= (int)app.quickJumpIndex.entries.size())
            continue;
        const auto& e = app.quickJumpIndex.entries[hit.index];
        const float y = rowsTop + rowH * row;
        const D2D1_RECT_F rowRect =
            D2D1::RectF(cardX + dpi(app, 8.0f), y, cardX + cardW - dpi(app, 8.0f),
                        y + rowH - dpi(app, 2.0f));
        if (hitIndex == q.selected)
            fillRound(app, rowRect, dpi(app, 6.0f), alpha(th.accent, 0.16f));

        const float badgeX = rowRect.left + dpi(app, 10.0f);
        const D2D1_RECT_F badge =
            D2D1::RectF(badgeX, y + dpi(app, 12.0f), badgeX + dpi(app, 30.0f),
                        y + dpi(app, 34.0f));
        fillRound(app, badge, dpi(app, 5.0f),
                  alpha(e.folder ? th.text : th.accent, 0.12f));
        std::wstring badgeText = e.folder ? L"DIR" : L"MD";
        if (!e.folder) {
            size_t dot = e.name.find_last_of(L'.');
            if (dot != std::wstring::npos && dot + 1 < e.name.size()) {
                badgeText = e.name.substr(dot + 1);
                for (auto& c : badgeText) c = (wchar_t)std::towupper(c);
                if (badgeText.size() > 3) badgeText.resize(3);
            }
        }
        drawText(app, badgeText, badgeFmt, badge, alpha(th.text, 0.66f));

        const float tx = rowRect.left + dpi(app, 50.0f);
        const float tw = rowRect.right - tx - dpi(app, 10.0f);
        drawText(app, e.name, titleFmt,
                 D2D1::RectF(tx, y + dpi(app, 5.0f), tx + tw, y + dpi(app, 25.0f)),
                 th.text);
        std::wstring sub = e.parent;
        if (!e.title.empty()) {
            sub = e.title;
            if (!e.parent.empty()) sub += L"  ·  " + e.parent;
        }
        if (sub.empty())
            sub = e.folder ? tr(app, "home.folder") : tr(app, "quickjump.root_item");
        drawText(app, sub, pathFmt,
                 D2D1::RectF(tx, y + dpi(app, 24.0f), tx + tw, y + dpi(app, 43.0f)),
                 alpha(th.text, 0.48f));
        q.rowHits.push_back({rowRect, hitIndex});
    }

    const float footerY = cardY + cardH - footerH;
    app.brush->SetColor(alpha(th.text, 0.10f));
    app.renderTarget->DrawLine(
        D2D1::Point2F(cardX + pad, footerY),
        D2D1::Point2F(cardX + cardW - pad, footerY), app.brush, 1.0f);
    // Status only complements rows; an empty list already explains itself.
    std::wstring status;
    if (!q.hits.empty())
        status = q.scanning ? tr(app, "quickjump.indexing")
                            : hitCountLabel(app, q.hits.size());
    float hintRight = cardX + cardW - pad;
    if (!status.empty()) {
        const float statusWidth = measureText(app, status, pathFmt);
        const float statusX =
            std::max(cardX + pad, cardX + cardW - pad - statusWidth);
        drawText(app, status, pathFmt,
                 D2D1::RectF(statusX, footerY, cardX + cardW - pad,
                             cardY + cardH),
                 alpha(th.text, 0.38f));
        hintRight = statusX - dpi(app, 12.0f);
    }
    if (hintRight > cardX + pad)
        drawText(app, tr(app, "quickjump.hint"), pathFmt,
                 D2D1::RectF(cardX + pad, footerY, hintRight, cardY + cardH),
                 alpha(th.text, 0.42f));

    if (titleFmt) titleFmt->Release();
    if (bodyFmt) bodyFmt->Release();
    if (messageFmt) messageFmt->Release();
    if (pathFmt) pathFmt->Release();
    if (badgeFmt) badgeFmt->Release();
}
