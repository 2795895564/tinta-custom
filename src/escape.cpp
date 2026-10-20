#include "escape.h"

namespace esc {
namespace {

struct Entry {
    Layer layer;
    Action action;
};

// The contract, top-most first. Call sites may add side effects, never a
// second priority: if this order changes, the tests in tests/escape_tests.cpp
// and the layering notes in docs/design-constitution.md change with it.
constexpr Entry kLayers[] = {
    {Layer::QuickJump, Action::Dismiss},
    {Layer::AppMenu, Action::Dismiss},
    {Layer::PanelResize, Action::Dismiss},
    {Layer::Lightbox, Action::Dismiss},
    {Layer::AnnotationEditor, Action::Dismiss},
    {Layer::CreateReference, Action::Dismiss},
    {Layer::PrintPreview, Action::Dismiss},
    {Layer::ContextMenu, Action::Dismiss},
    {Layer::ShortcutCapture, Action::Dismiss},
    {Layer::ShortcutEditor, Action::Dismiss},
    {Layer::ThemeEditor, Action::Dismiss},
    {Layer::Settings, Action::Dismiss},
    {Layer::Help, Action::Dismiss},
    {Layer::ThemeChooser, Action::Dismiss},
    {Layer::TabSwitcher, Action::Dismiss},
    {Layer::TabDrag, Action::Dismiss},
    {Layer::TabMenu, Action::Dismiss},
    {Layer::FolderBrowserInput, Action::Dismiss},
    {Layer::Search, Action::Dismiss},
    {Layer::FolderBrowser, Action::Dismiss},
    {Layer::Toc, Action::Dismiss},
    {Layer::Zen, Action::Dismiss},
    {Layer::Editor, Action::Dismiss},
    {Layer::Reader, Action::GoHome},
    {Layer::Home, Action::None},
};

constexpr int kEntryCount = static_cast<int>(sizeof(kLayers) / sizeof(kLayers[0]));

}  // namespace

Layer topmost(const Levels& levels) {
    for (int i = 0; i < kEntryCount; ++i) {
        if (levels.open(kLayers[i].layer)) return kLayers[i].layer;
    }
    return Layer::None;
}

Decision decide(const Levels& levels) {
    for (int i = 0; i < kEntryCount; ++i) {
        if (!levels.open(kLayers[i].layer)) continue;
        return {kLayers[i].layer, kLayers[i].action};
    }
    return {};
}

int indexOf(Layer layer) {
    for (int i = 0; i < kEntryCount; ++i) {
        if (kLayers[i].layer == layer) return i;
    }
    return -1;
}

const char* layerName(Layer layer) {
    switch (layer) {
        case Layer::None: return "none";
        case Layer::QuickJump: return "quickjump";
        case Layer::AppMenu: return "appmenu";
        case Layer::PanelResize: return "panelresize";
        case Layer::Lightbox: return "lightbox";
        case Layer::AnnotationEditor: return "annotation-editor";
        case Layer::CreateReference: return "create-reference";
        case Layer::PrintPreview: return "print-preview";
        case Layer::ContextMenu: return "context-menu";
        case Layer::ShortcutCapture: return "shortcut-capture";
        case Layer::ShortcutEditor: return "shortcut-editor";
        case Layer::ThemeEditor: return "theme-editor";
        case Layer::Settings: return "settings";
        case Layer::Help: return "help";
        case Layer::ThemeChooser: return "theme-chooser";
        case Layer::TabSwitcher: return "tab-switcher";
        case Layer::TabDrag: return "tab-drag";
        case Layer::TabMenu: return "tab-menu";
        case Layer::FolderBrowserInput: return "folder-browser-input";
        case Layer::Search: return "search";
        case Layer::FolderBrowser: return "folder-browser";
        case Layer::Toc: return "toc";
        case Layer::Zen: return "zen";
        case Layer::Editor: return "editor";
        case Layer::Reader: return "reader";
        case Layer::Home: return "home";
        case Layer::Count: break;
    }
    return "unknown";
}

}  // namespace esc
