#ifndef TINTA_PREVIEW_FOLLOW_H
#define TINTA_PREVIEW_FOLLOW_H

#include "app.h"

// Height of the document viewport the preview scrolls in: the docked edit
// preview starts below the tab strip, everything else uses the whole client
// area.
inline float previewViewportHeight(const App& app) {
    if (editSplitPreview(app)) {
        return std::max(1.0f, (float)app.height - chromeTopHeight(app));
    }
    return static_cast<float>(app.height);
}

// Source byte offset of the editor caret, or SIZE_MAX when the line table
// does not match the buffer (an Enter/delete changed the line count).
size_t previewCaretByteOffset(const App& app);

// Index into scrollAnchors of the top-level block holding the caret.
int previewCaretBlockIndex(const App& app);

// Rendered Y of the caret, using the finest anchor available.
bool previewYForCaret(const App& app, float& outY);

// Document-space bounds of the caret's block.
bool previewCaretBlockRange(const App& app, float& top, float& bottom);

// Comfort-band scroll target for a caret Y (pure, for tests).
// Scroll target that puts the caret's Markdown block near the top of the
// preview; a block taller than the pane keeps the caret line in view instead.
float previewBlockTargetY(const App& app, float blockTop, float blockBottom,
                          float caretY, float paneTop, float paneHeight);

// Reset the state machine when entering edit mode, switching tabs, or toggling
// the setting.
void previewFollowReset(App& app);

// Remember the source line at the top of the preview before an image or
// diagram reflow, so the new height does not scroll the page by itself.
void previewCaptureReflowAnchor(App& app);

// Rendered Y for an arbitrary source byte offset (used by the reflow guard).
bool previewYForSourceOffset(const App& app, size_t sourceOffset, float& outY);

// Per-frame entry point; replaces the old page-top-only sync in render().
void updatePreviewFollow(App& app);

#endif  // TINTA_PREVIEW_FOLLOW_H
