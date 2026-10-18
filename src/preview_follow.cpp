#include "preview_follow.h"

#include "editor.h"

#include <algorithm>
#include <cmath>

namespace {

size_t lineForPos(const App& app, size_t pos) {
    if (app.editorLineStarts.empty()) return 0;
    size_t lo = 0, hi = app.editorLineStarts.size();
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (app.editorLineStarts[mid] <= pos) lo = mid;
        else hi = mid;
    }
    return std::min(lo, app.editorLineStarts.size() - 1);
}

size_t lineForByte(const App& app, size_t byte) {
    if (app.editorLineByteOffsets.empty() || app.editorLineStarts.empty()) return 0;
    auto it = std::upper_bound(app.editorLineByteOffsets.begin(),
                               app.editorLineByteOffsets.end(), byte);
    size_t line = it == app.editorLineByteOffsets.begin()
                      ? 0
                      : static_cast<size_t>(it - app.editorLineByteOffsets.begin() - 1);
    return std::min(line, app.editorLineStarts.size() - 1);
}

// Byte offset of the end of a logical line (before its newline). The end,
// not the start, anchors markup-prefixed blocks such as headings and list
// items: their first text node sits past the "## " marker.
size_t lineEndByte(const App& app, size_t line) {
    if (line + 1 < app.editorLineByteOffsets.size()) {
        return app.editorLineByteOffsets[line + 1] - 1;
    }
    return SIZE_MAX;
}

size_t utf8PrefixBytes(const App& app, size_t from, size_t to) {
    size_t bytes = 0;
    for (size_t i = from; i < to && i < app.editorText.size(); i++) {
        wchar_t c = app.editorText[i];
        if (c < 0x80) {
            bytes += 1;
        } else if (c < 0x800) {
            bytes += 2;
        } else if (c >= 0xD800 && c <= 0xDBFF && i + 1 < to &&
                   app.editorText[i + 1] >= 0xDC00 &&
                   app.editorText[i + 1] <= 0xDFFF) {
            bytes += 4;
            i++;
        } else {
            bytes += 3;
        }
    }
    return bytes;
}

// Y of a caret line inside a block, by line ratio. Used when the block has
// no anchor at or after the caret (typically the document's last block).
float blockLineRatioY(const App& app, int block, size_t caretLine) {
    const auto& anchors = app.scrollAnchors;
    if (block < 0 || block >= (int)anchors.size()) return app.contentHeight;
    float top = anchors[block].renderedY;
    float bottom = app.contentHeight;
    for (size_t i = block + 1; i < anchors.size(); i++) {
        if (anchors[i].blockStart) {
            bottom = anchors[i].renderedY;
            break;
        }
    }
    size_t startLine = lineForByte(app, anchors[block].sourceOffset);
    size_t endLine = app.editorLineStarts.empty()
                         ? startLine
                         : app.editorLineStarts.size() - 1;
    for (size_t i = block + 1; i < anchors.size(); i++) {
        if (anchors[i].blockStart) {
            endLine = lineForByte(app, anchors[i].sourceOffset);
            break;
        }
    }
    if (endLine <= startLine) return top;
    if (caretLine <= startLine) return top;
    if (caretLine >= endLine) return bottom;
    float t = float(caretLine - startLine) / float(endLine - startLine);
    return top + t * (bottom - top);
}

// The old page-top sync, kept for the Page state.
float pageTargetY(App& app) {
    if (app.scrollAnchors.empty() || app.editorLineByteOffsets.empty()) {
        return app.scrollY;
    }
    int topLine = (int)editorTopVisibleLine(app);
    topLine = std::max(0, std::min(topLine,
                                   (int)app.editorLineByteOffsets.size() - 1));
    size_t topByteOffset = app.editorLineByteOffsets[topLine];

    size_t lo = 0, hi = app.scrollAnchors.size();
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (app.scrollAnchors[mid].sourceOffset <= topByteOffset) lo = mid;
        else hi = mid;
    }

    float targetY;
    if (lo + 1 < app.scrollAnchors.size() &&
        app.scrollAnchors[lo + 1].sourceOffset > app.scrollAnchors[lo].sourceOffset) {
        float t = float(topByteOffset - app.scrollAnchors[lo].sourceOffset) /
                  float(app.scrollAnchors[lo + 1].sourceOffset -
                        app.scrollAnchors[lo].sourceOffset);
        t = std::clamp(t, 0.0f, 1.0f);
        targetY = app.scrollAnchors[lo].renderedY +
                  t * (app.scrollAnchors[lo + 1].renderedY -
                       app.scrollAnchors[lo].renderedY);
    } else {
        targetY = app.scrollAnchors[lo].renderedY;
        if (app.contentHeight > app.scrollAnchors[lo].renderedY) {
            size_t lastOffset = app.scrollAnchors[lo].sourceOffset;
            size_t totalBytes = app.editorLineByteOffsets.back();
            if (totalBytes > lastOffset) {
                float t = float(topByteOffset - lastOffset) /
                          float(totalBytes - lastOffset);
                t = std::clamp(t, 0.0f, 1.0f);
                targetY += t * (app.contentHeight -
                                app.scrollAnchors[lo].renderedY);
            }
        }
    }

    float paneH = previewViewportHeight(app);
    float previewMaxScroll = std::max(0.0f, app.contentHeight - paneH);
    float alignY = targetY - documentContentTop(app);
    float synced = std::clamp(alignY, 0.0f, previewMaxScroll);
    if (app.editorScrollY <= 0.5f) synced = 0.0f;
    float editorMax = std::max(0.0f, app.editorContentHeight - app.height);
    if (app.editorScrollY >= editorMax - 1.0f) {
        return std::clamp(std::max(app.scrollY, synced), 0.0f, previewMaxScroll);
    }
    return synced;
}

}  // namespace

size_t previewCaretByteOffset(const App& app) {
    if (app.editorLineStarts.empty() || app.editorLineByteOffsets.empty()) {
        return SIZE_MAX;
    }
    // The byte table is rebuilt by the reparse debounce. If it no longer
    // matches the buffer's line count, an Enter/delete moved the blocks and
    // this frame falls back to Page.
    if (app.editorLineByteOffsets.size() != app.editorLineStarts.size()) {
        return SIZE_MAX;
    }
    size_t line = lineForPos(app, app.editorCursorPos);
    if (line >= app.editorLineByteOffsets.size()) return SIZE_MAX;
    size_t lineStart = app.editorLineStarts[line];
    if (app.editorCursorPos < lineStart) return SIZE_MAX;
    return app.editorLineByteOffsets[line] +
           utf8PrefixBytes(app, lineStart, app.editorCursorPos);
}

int previewCaretBlockIndex(const App& app) {
    if (app.scrollAnchors.empty() || app.editorLineByteOffsets.empty() ||
        app.editorLineStarts.empty()) {
        return -1;
    }
    size_t caretLine = lineForPos(app, app.editorCursorPos);
    if (caretLine >= app.editorLineByteOffsets.size()) return -1;
    size_t caretByte = lineEndByte(app, caretLine);

    size_t lo = 0, hi = app.scrollAnchors.size();
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (app.scrollAnchors[mid].sourceOffset <= caretByte) lo = mid;
        else hi = mid;
    }
    while (lo > 0 && !app.scrollAnchors[lo].blockStart) lo--;
    return app.scrollAnchors[lo].blockStart ? (int)lo : -1;
}

bool previewCaretBlockRange(const App& app, float& top, float& bottom) {
    int block = previewCaretBlockIndex(app);
    if (block < 0) return false;
    top = app.scrollAnchors[block].renderedY;
    bottom = app.contentHeight;
    for (size_t i = block + 1; i < app.scrollAnchors.size(); i++) {
        if (app.scrollAnchors[i].blockStart) {
            bottom = app.scrollAnchors[i].renderedY;
            break;
        }
    }
    return true;
}

bool previewYForCaret(const App& app, float& outY) {
    size_t caretByte = previewCaretByteOffset(app);
    if (caretByte == SIZE_MAX) return false;
    int block = previewCaretBlockIndex(app);
    if (block < 0 || app.scrollAnchors.empty()) return false;

    const auto& anchors = app.scrollAnchors;
    // A caret on a heading/list marker sits before the block's first text
    // anchor; the block top is the right answer there.
    if (caretByte < anchors[block].sourceOffset) {
        outY = anchors[block].renderedY;
        return true;
    }

    auto hi = std::upper_bound(
        anchors.begin(), anchors.end(), caretByte,
        [](size_t byte, const App::ScrollAnchor& anchor) {
            return byte < anchor.sourceOffset;
        });

    float y = app.contentHeight;
    if (hi == anchors.end()) {
        y = blockLineRatioY(app, block, lineForPos(app, app.editorCursorPos));
    } else if (hi == anchors.begin()) {
        y = anchors.front().renderedY;
    } else {
        const auto& lo = *(hi - 1);
        if (hi->sourceOffset <= lo.sourceOffset) {
            y = lo.renderedY;
        } else {
            float t = float(caretByte - lo.sourceOffset) /
                      float(hi->sourceOffset - lo.sourceOffset);
            t = std::clamp(t, 0.0f, 1.0f);
            y = lo.renderedY + t * (hi->renderedY - lo.renderedY);
        }
    }

    float top = 0, bottom = 0;
    if (previewCaretBlockRange(app, top, bottom)) {
        y = std::clamp(y, top, bottom);
    }
    outY = y;
    return true;
}

float previewBlockTargetY(const App& app, float blockTop, float blockBottom,
                          float caretY, float paneTop, float paneHeight) {
    if (paneHeight <= 0.0f) return app.scrollY;
    const float margin = dpi(app, 10.0f);
    float target;
    if (blockBottom - blockTop > paneHeight - 2.0f * margin) {
        // A block taller than the pane (long paragraph, code block, table):
        // keep the caret line itself in view, with reading context above.
        target = caretY - (paneTop + 0.25f * paneHeight);
    } else {
        // Short heading/paragraph/list/table: its top sits near the pane top,
        // leaving the following content visible.
        target = blockTop - paneTop - margin;
        if (caretY - target > paneTop + paneHeight - margin) {
            target = caretY - (paneTop + 0.25f * paneHeight);
        }
    }
    float maxScroll = std::max(0.0f, app.contentHeight - paneHeight);
    return std::clamp(target, 0.0f, maxScroll);
}

bool previewYForSourceOffset(const App& app, size_t sourceOffset, float& outY) {
    const auto& anchors = app.scrollAnchors;
    if (anchors.empty()) return false;
    auto hi = std::upper_bound(
        anchors.begin(), anchors.end(), sourceOffset,
        [](size_t offset, const App::ScrollAnchor& anchor) {
            return offset < anchor.sourceOffset;
        });
    if (hi == anchors.begin()) {
        outY = anchors.front().renderedY;
        return true;
    }
    if (hi == anchors.end()) {
        outY = app.contentHeight;
        return true;
    }
    const auto& lo = *(hi - 1);
    if (hi->sourceOffset <= lo.sourceOffset) {
        outY = lo.renderedY;
    } else {
        float t = float(sourceOffset - lo.sourceOffset) /
                  float(hi->sourceOffset - lo.sourceOffset);
        t = std::clamp(t, 0.0f, 1.0f);
        outY = lo.renderedY + t * (hi->renderedY - lo.renderedY);
    }
    return true;
}

void previewCaptureReflowAnchor(App& app) {
    if (!editSplitPreview(app) || app.scrollAnchors.empty()) {
        app.previewPreserveActive = false;
        return;
    }
    if (app.previewPreserveActive) return;
    const auto& anchors = app.scrollAnchors;
    float paneTop = chromeTopHeight(app);
    float docY = app.scrollY + paneTop + 1.0f;
    size_t source = anchors.front().sourceOffset;
    for (size_t i = 0; i < anchors.size(); i++) {
        if (anchors[i].renderedY <= docY) {
            source = anchors[i].sourceOffset;
        } else if (i > 0) {
            const auto& lo = anchors[i - 1];
            const auto& hi = anchors[i];
            if (hi.renderedY > lo.renderedY) {
                float t = (docY - lo.renderedY) / (hi.renderedY - lo.renderedY);
                t = std::clamp(t, 0.0f, 1.0f);
                source = lo.sourceOffset +
                         (size_t)(t * float(hi.sourceOffset - lo.sourceOffset));
            }
            break;
        } else {
            break;
        }
    }
    app.previewPreserveSource = source;
    app.previewPreserveCaret = app.editorCursorPos;
    app.previewPreserveScreenY = docY - app.scrollY;
    app.previewPreserveActive = true;
}

void previewFollowReset(App& app) {
    app.previewFollowState = App::PreviewFollowState::Caret;
    app.previewFollowInitialized = false;
    app.previewFollowUserScrolled = false;
    app.previewPreserveActive = false;
    app.previewFollowLastCaret = app.editorCursorPos;
    app.previewFollowLastEditorScroll = app.editorScrollY;
    app.previewFollowLastTableEdit = app.tableEditActive;
}

void updatePreviewFollow(App& app) {
    // The split edit preview only: the reader (and print, and the full-width
    // reading view) owns its own scroll.
    if (!editSplitPreview(app)) return;

    float paneTop = chromeTopHeight(app);
    float paneH = previewViewportHeight(app);

    // An image or diagram just changed the rendered height. Keep the same
    // source line at the top of the page unless the caret moved or the user
    // scrolled while the reflow was pending.
    if (app.previewPreserveActive) {
        bool caretChanged = app.editorCursorPos != app.previewPreserveCaret;
        app.previewPreserveActive = false;
        if (!caretChanged && !app.previewFollowUserScrolled) {
            float y = 0;
            if (previewYForSourceOffset(app, app.previewPreserveSource, y)) {
                float target = y - app.previewPreserveScreenY;
                float maxScroll = std::max(0.0f, app.contentHeight - paneH);
                app.scrollY = std::clamp(target, 0.0f, maxScroll);
                app.targetScrollY = app.scrollY;
            }
            app.previewFollowLastCaret = app.editorCursorPos;
            app.previewFollowLastEditorScroll = app.editorScrollY;
            app.previewFollowLastTableEdit = app.tableEditActive;
            return;
        }
    }

    if (!app.previewFollowInitialized) {
        app.previewFollowState = App::PreviewFollowState::Caret;
        app.previewFollowInitialized = true;
        app.previewFollowLastCaret = app.editorCursorPos;
        app.previewFollowLastEditorScroll = app.editorScrollY;
        app.previewFollowLastTableEdit = app.tableEditActive;
    } else {
        bool caretMoved = app.editorCursorPos != app.previewFollowLastCaret;
        bool tableEdit = app.tableEditActive;
        bool tableEditEnded = app.previewFollowLastTableEdit && !tableEdit;
        bool previewScrollbar = app.scrollbarDragging || app.hScrollbarDragging;

        if (tableEdit) {
            // The caret is in the rendered cell, not in the source: leave the
            // page still while the cell editor is up.
            app.previewFollowState = App::PreviewFollowState::Manual;
        } else if (previewScrollbar && !caretMoved) {
            // The user grabbed the preview's scrollbar: let them read ahead
            // until the caret moves again.
            app.previewFollowState = App::PreviewFollowState::Manual;
        } else if (caretMoved || tableEditEnded) {
            app.previewFollowState = App::PreviewFollowState::Caret;
            app.previewFollowUserScrolled = false;
        } else if (app.previewFollowUserScrolled) {
            // Wheel/scrollbar moved the source away from the caret: follow the
            // reading position, not the caret.
            app.previewFollowState = App::PreviewFollowState::Page;
        }
    }

    float target = app.scrollY;
    switch (app.previewFollowState) {
        case App::PreviewFollowState::Manual:
            app.previewFollowLastCaret = app.editorCursorPos;
            app.previewFollowLastEditorScroll = app.editorScrollY;
            app.previewFollowLastTableEdit = app.tableEditActive;
            return;
        case App::PreviewFollowState::Page:
            target = pageTargetY(app);
            break;
        case App::PreviewFollowState::Caret: {
            float blockTop = 0, blockBottom = 0, caretY = 0;
            if (previewCaretBlockRange(app, blockTop, blockBottom) &&
                previewYForCaret(app, caretY)) {
                target = previewBlockTargetY(app, blockTop, blockBottom, caretY,
                                             paneTop, paneH);
            } else {
                // The line table is stale until the reparse lands. Holding the
                // current position avoids the old page-top jump; the next
                // reparse re-targets the caret's block.
                target = app.scrollY;
            }
            break;
        }
    }

    float maxScroll = std::max(0.0f, app.contentHeight - paneH);
    target = std::clamp(target, 0.0f, maxScroll);
    app.scrollY = target;
    app.targetScrollY = target;

    app.previewFollowLastCaret = app.editorCursorPos;
    app.previewFollowLastEditorScroll = app.editorScrollY;
    app.previewFollowLastTableEdit = app.tableEditActive;
}
