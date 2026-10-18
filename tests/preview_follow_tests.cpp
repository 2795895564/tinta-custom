// Preview follow: the split editor's page tracks the caret in a comfort
// band, follows the page after the user scrolls away, and leaves the page
// alone while the preview scrollbar or a table cell is being used.
#include "d2d_init.h"
#include "editor.h"
#include "preview_follow.h"
#include "render.h"
#include "settings.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {

int failures = 0;

void check(bool value, const char* message) {
    if (value) return;
    if (failures < 40) std::cerr << "FAIL preview follow: " << message << '\n';
    failures++;
}

size_t lineContaining(const App& app, const std::wstring& needle) {
    for (size_t line = 0; line < app.editorLineStarts.size(); line++) {
        size_t start = app.editorLineStarts[line];
        size_t end = line + 1 < app.editorLineStarts.size()
                         ? app.editorLineStarts[line + 1]
                         : app.editorText.size();
        if (start <= end && needle.size() <= end - start &&
            app.editorText.compare(start, needle.size(), needle) == 0) {
            return line;
        }
    }
    return app.editorLineStarts.size() - 1;
}

void caretToLine(App& app, size_t line) {
    line = std::min(line, app.editorLineStarts.size() - 1);
    app.editorCursorPos = app.editorLineStarts[line];
    app.editorCaretUpstreamPos = std::wstring::npos;
    app.editorDesiredCol = -1;
}

}  // namespace

int runPreviewFollowTests() {
    OleInitialize(nullptr);
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Preview follow tests", WS_POPUP,
                               0, 0, 1050, 700, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 700;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);

    std::ifstream input(TINTA_PREVIEW_FOLLOW_FIXTURE, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    const std::wstring source = toWide(bytes);
    check(!source.empty(), "the preview-follow fixture loads");
    if (source.empty()) return 1;

    app.currentFile = TINTA_PREVIEW_FOLLOW_FIXTURE;
    app.editMode = true;
    app.editorShowPreview = true;
    app.editorReadingPreview = false;
    restoreEditBuffer(app, source, false, 0, 0);
    ensureLayoutComplete(app);
    check(!app.scrollAnchors.empty(), "the fixture lays out anchors");
    check(app.contentHeight > app.height, "the fixture is taller than the pane");

    bool hasBlockStart = false;
    for (const auto& anchor : app.scrollAnchors) {
        if (anchor.blockStart) hasBlockStart = true;
    }
    check(hasBlockStart, "top-level block anchors mark blockStart");

    // --- Preview block target, with a synthetic page so the maths is
    // unambiguous ---
    const float savedContentHeight = app.contentHeight;
    const float savedScrollY = app.scrollY;
    app.contentHeight = 3000.0f;
    app.scrollY = 1000.0f;
    const float paneTop = 40.0f;
    const float paneH = 800.0f;
    const float margin = dpi(app, 10.0f);

    float target = previewBlockTargetY(app, 1300.0f, 1500.0f, 1400.0f,
                                       paneTop, paneH);
    check(std::fabs(target - (1300.0f - paneTop - margin)) < 0.5f,
          "a short block is placed at the top of the preview");

    target = previewBlockTargetY(app, 1100.0f, 2500.0f, 1800.0f,
                                  paneTop, paneH);
    check(std::fabs(target - (1800.0f - (paneTop + 0.25f * paneH))) < 0.5f,
          "a tall block keeps the caret line in view");

    target = previewBlockTargetY(app, 2800.0f, 3000.0f, 2900.0f,
                                  paneTop, paneH);
    check(std::fabs(target - 2200.0f) < 0.5f,
          "the target clamps at the document end");
    app.contentHeight = savedContentHeight;
    app.scrollY = savedScrollY;

    // --- Caret block follow with the real fixture ---
    const float uiPaneTop = chromeTopHeight(app);
    const float uiMargin = dpi(app, 10.0f);
    app.editorCenterCaret = true;
    app.scrollbarDragging = false;
    app.tableEditActive = false;
    previewFollowReset(app);
    caretToLine(app, lineContaining(app, L"## Middle paragraph"));
    updatePreviewFollow(app);
    check(app.previewFollowState == App::PreviewFollowState::Caret,
          "a caret move enters Caret state");

    float blockTop = 0, blockBottom = 0;
    check(previewCaretBlockRange(app, blockTop, blockBottom),
          "the caret block has bounds");
    check(std::fabs((blockTop - app.scrollY) - (uiPaneTop + uiMargin)) < 2.0f,
          "the caret's Markdown block sits near the preview top");

    caretToLine(app, lineContaining(app, L"## Closing paragraph"));
    updatePreviewFollow(app);
    check(app.previewFollowState == App::PreviewFollowState::Caret,
          "a second caret move stays in Caret state");

    // A layout-driven scroll change alone must not switch to Page: images
    // and reflow can move editorScrollY without the user scrolling.
    app.editorContentHeight = std::max(app.editorContentHeight, 3000.0f);
    app.editorScrollY += 200.0f;
    updatePreviewFollow(app);
    check(app.previewFollowState == App::PreviewFollowState::Caret,
          "layout-driven editor scroll does not leave Caret state");

    // --- Scrolling the source away falls back to page sync ---
    const float editorMax = std::max(0.0f, app.editorContentHeight - app.height);
    app.editorScrollY = std::min(editorMax, app.editorScrollY + 300.0f);
    app.previewFollowUserScrolled = true;
    updatePreviewFollow(app);
    check(app.previewFollowState == App::PreviewFollowState::Page,
          "scrolling the source without moving the caret enters Page state");

    caretToLine(app, lineContaining(app, L"## Opening paragraph"));
    updatePreviewFollow(app);
    check(app.previewFollowState == App::PreviewFollowState::Caret,
          "moving the caret after scrolling returns to Caret");
    check(!app.previewFollowUserScrolled,
          "a caret move clears the user-scroll flag");

    // --- An image/diagram reflow keeps the page on the same line ---
    caretToLine(app, lineContaining(app, L"## Middle paragraph"));
    updatePreviewFollow(app);
    const float reflowStart = app.scrollY;
    previewCaptureReflowAnchor(app);
    check(app.previewPreserveActive,
          "an image reflow captures the preview's top source line");
    updatePreviewFollow(app);
    check(!app.previewPreserveActive,
          "the reflow guard is consumed by the next frame");
    check(std::fabs(app.scrollY - reflowStart) < 1.0f,
          "an image reflow does not scroll the preview by itself");

    // --- Dragging the preview scrollbar suspends follow ---
    app.scrollbarDragging = true;
    app.scrollY = std::min(savedContentHeight, app.scrollY + 80.0f);
    const float manualY = app.scrollY;
    updatePreviewFollow(app);
    check(app.previewFollowState == App::PreviewFollowState::Manual,
          "a preview scrollbar drag enters Manual state");
    check(std::fabs(app.scrollY - manualY) < 0.5f,
          "Manual state leaves the preview scroll alone");
    app.scrollbarDragging = false;
    caretToLine(app, lineContaining(app, L"## Closing paragraph"));
    updatePreviewFollow(app);
    check(app.previewFollowState == App::PreviewFollowState::Caret,
          "moving the caret after a manual scroll resumes follow");

    // --- A table cell editor pauses, then resumes on commit ---
    app.tableEditActive = true;
    updatePreviewFollow(app);
    check(app.previewFollowState == App::PreviewFollowState::Manual,
          "editing a table cell suspends preview follow");
    app.tableEditActive = false;
    updatePreviewFollow(app);
    check(app.previewFollowState == App::PreviewFollowState::Caret,
          "committing the cell resumes caret follow");

    // --- Block bounds and fine anchors ---
    caretToLine(app, lineContaining(app, L"## A code block"));
    float top = 0, bottom = 0;
    check(previewCaretBlockRange(app, top, bottom) && bottom > top,
          "the caret block has ordered bounds");

    caretToLine(app, lineContaining(app, L"int main()"));
    float y1 = 0;
    check(previewYForCaret(app, y1), "a code-line caret maps to a Y");
    caretToLine(app, lineContaining(app, L"return total;"));
    float y2 = 0;
    check(previewYForCaret(app, y2) && y2 > y1,
          "later code lines map further down the page");

    // --- The editor pane pins the caret line at the same fraction ---
    app.editorWordWrap = false;
    app.editorContentHeight = 4000.0f;
    app.editorCenterCaret = true;
    app.editorCaretCenterRatio = 0.5f;
    const size_t middleLine = lineContaining(app, L"## Middle paragraph");
    caretToLine(app, middleLine);
    editorPinCaret(app);
    float lineHeight = app.editorTextFormat
        ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    float cursorY = dpi(app, 8.0f) + static_cast<float>(middleLine) * lineHeight;
    float viewportH = static_cast<float>(app.height) - chromeTopHeight(app);
    float expected = cursorY - 0.5f * viewportH;
    float maxScroll = std::max(0.0f, app.editorContentHeight - viewportH);
    float visibleMax = cursorY + lineHeight +
                       std::max(lineHeight, readPillClearance(app)) - viewportH;
    maxScroll = std::max(maxScroll, visibleMax);
    expected = std::max(0.0f, std::min(expected, maxScroll));
    check(std::fabs(app.editorScrollY - expected) < 1.5f,
          "the editor centers the caret at 50% of its viewport");

    app.editorCaretCenterRatio = 0.6f;
    editorPinCaret(app);
    expected = cursorY - 0.6f * viewportH;
    expected = std::max(0.0f, std::min(expected, maxScroll));
    check(std::fabs(app.editorScrollY - expected) < 1.5f,
          "the editor follows the configured caret ratio");

    // The last line must still reach the configured height, using the
    // virtual bottom space that renderEditor adds when centering is on.
    app.editorCaretCenterRatio = 0.5f;
    caretToLine(app, app.editorLineStarts.size() - 1);
    app.renderTarget->BeginDraw();
    renderEditor(app, 500.0f);
    check(SUCCEEDED(app.renderTarget->EndDraw()),
          "the editor renders for the end-centering check");
    editorPinCaret(app);
    float endCursorY = dpi(app, 8.0f) +
        static_cast<float>(app.editorLineStarts.size() - 1) * lineHeight;
    float endScreen = endCursorY - app.editorScrollY;
    check(std::fabs(endScreen - 0.5f * viewportH) < 2.0f,
          "the last line still reaches the configured height");

    app.editorCenterCaret = false;
    app.editorScrollY = 0.0f;
    editorPinCaret(app);
    check(std::fabs(app.editorScrollY) < 0.5f,
          "centering off leaves the centering helper inert");
    app.editorCenterCaret = true;
    app.editorCaretCenterRatio = 0.5f;

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    OleUninitialize();
    std::cout << "Preview follow: " << failures << " failures\n";
    return failures ? 1 : 0;
}
