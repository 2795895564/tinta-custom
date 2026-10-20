#ifndef TINTA_ESCAPE_H
#define TINTA_ESCAPE_H

// Esc is one decision, not sixteen.
//
// Every layer that can own the keyboard is registered here, top-most first.
// The order is the contract: when two layers are open, only the earliest one
// answers Esc, no matter which flag was set first. Esc either dismisses that
// layer or falls back to the document stack; there is no Esc path to quitting,
// closing a window, or closing a document.
//
// This header is pure: no Win32, no App. Callers map their own state onto
// Levels (see escapeLevels in input.cpp) and keep only side effects.

namespace esc {

enum class Layer : unsigned {
    None = 0,
    QuickJump,
    AppMenu,
    PanelResize,
    Lightbox,
    AnnotationEditor,
    CreateReference,
    PrintPreview,
    ContextMenu,
    ShortcutCapture,
    ShortcutEditor,
    ThemeEditor,
    Settings,
    Help,
    ThemeChooser,
    TabSwitcher,
    TabDrag,
    TabMenu,
    FolderBrowserInput,
    Search,
    FolderBrowser,
    Toc,
    Zen,
    Editor,  // base layer: editing the document
    Reader,  // base layer: reading the document
    Home,    // base layer: Home tab; the repo picker shares it (Esc stays put)
    Count
};

enum class Action {
    None,     // bottom of the stack (Home / picker): Esc is a no-op
    Dismiss,  // close, or step out of, the layer that owns Esc
    GoHome,   // nothing left to close: activate the fixed Home tab
};

struct Levels {
    unsigned bits = 0;

    static constexpr unsigned bit(Layer layer) {
        return 1u << static_cast<unsigned>(layer);
    }
    Levels with(Layer layer) const {
        Levels next = *this;
        next.bits |= bit(layer);
        return next;
    }
    bool open(Layer layer) const { return (bits & bit(layer)) != 0; }
};

struct Decision {
    Layer layer = Layer::None;
    Action action = Action::None;
};

// Top-most open layer, or Layer::None when nothing is registered.
Layer topmost(const Levels& levels);

// What Esc does in this snapshot. Never returns a quit or close action.
Decision decide(const Levels& levels);

// Table position of a layer (0 = top-most); -1 when the layer has no entry.
// Tests use this to prove the table covers every layer exactly once.
int indexOf(Layer layer);

const char* layerName(Layer layer);

static_assert(static_cast<unsigned>(Layer::Count) <= 32,
              "Levels stores one bit per layer");

}  // namespace esc

#endif
