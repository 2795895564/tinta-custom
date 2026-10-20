#include "escape.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;
void check(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

using esc::Action;
using esc::Layer;
using esc::Levels;

// "Esc answers exactly one layer": the winner must be the one with the lowest
// table position among the open layers.
bool isTopmostOf(const Levels& levels, Layer expected) {
    int best = -1;
    for (unsigned i = 1; i < static_cast<unsigned>(Layer::Count); ++i) {
        Layer layer = static_cast<Layer>(i);
        if (!levels.open(layer)) continue;
        int at = esc::indexOf(layer);
        if (at >= 0 && (best < 0 || at < best)) best = at;
    }
    if (best < 0) return expected == Layer::None;
    return esc::indexOf(expected) == best;
}

}  // namespace

int main() {
    // Nothing open, no base: "do nothing" is a real answer.
    check(esc::decide(Levels{}).layer == Layer::None, "empty stack answers none");
    check(esc::decide(Levels{}).action == Action::None, "empty stack does nothing");

    // Base layers: only the reader climbs back to Home.
    check(esc::decide(Levels{}.with(Layer::Home)).action == Action::None,
          "Home is the bottom of the stack: Esc stays put");
    check(esc::decide(Levels{}.with(Layer::Editor)).layer == Layer::Editor,
          "editor owns Esc");
    check(esc::decide(Levels{}.with(Layer::Editor)).action == Action::Dismiss,
          "editor Esc leaves edit mode");
    check(esc::decide(Levels{}.with(Layer::Reader)).action == Action::GoHome,
          "reader Esc falls back to Home");

    // An overlay always wins over the base layer underneath it.
    check(esc::decide(Levels{}.with(Layer::Reader).with(Layer::Settings)).layer ==
              Layer::Settings,
          "settings covers the reader");
    check(esc::decide(Levels{}.with(Layer::Editor).with(Layer::QuickJump)).layer ==
              Layer::QuickJump,
          "quick jump covers the editor");

    // The documented priority, pair by pair.
    struct Pair {
        Layer upper;
        Layer lower;
    };
    const Pair ordered[] = {
        {Layer::QuickJump, Layer::AppMenu},
        {Layer::AppMenu, Layer::PanelResize},
        {Layer::PanelResize, Layer::Lightbox},
        {Layer::Lightbox, Layer::AnnotationEditor},
        {Layer::AnnotationEditor, Layer::CreateReference},
        {Layer::CreateReference, Layer::PrintPreview},
        {Layer::PrintPreview, Layer::ContextMenu},
        {Layer::ContextMenu, Layer::ShortcutCapture},
        {Layer::ShortcutCapture, Layer::ShortcutEditor},
        {Layer::ShortcutEditor, Layer::ThemeEditor},
        {Layer::ThemeEditor, Layer::Settings},
        {Layer::Settings, Layer::Help},
        {Layer::Help, Layer::ThemeChooser},
        {Layer::ThemeChooser, Layer::TabSwitcher},
        {Layer::TabSwitcher, Layer::TabDrag},
        {Layer::TabDrag, Layer::TabMenu},
        {Layer::TabMenu, Layer::FolderBrowserInput},
        {Layer::FolderBrowserInput, Layer::Search},
        {Layer::Search, Layer::FolderBrowser},
        {Layer::FolderBrowser, Layer::Toc},
        {Layer::Toc, Layer::Zen},
        {Layer::Zen, Layer::Editor},
        {Layer::Editor, Layer::Reader},
        {Layer::Reader, Layer::Home},
    };
    for (const Pair& pair : ordered) {
        Levels both = Levels{}.with(pair.lower).with(pair.upper);
        check(esc::decide(both).layer == pair.upper,
              "the earlier table entry owns Esc regardless of open order");
        Levels reversed = Levels{}.with(pair.upper).with(pair.lower);
        check(esc::decide(reversed).layer == pair.upper,
              "Esc does not depend on which flag was set first");
    }

    // Every layer has exactly one table entry, and it is reachable.
    for (unsigned i = 1; i < static_cast<unsigned>(Layer::Count); ++i) {
        Layer layer = static_cast<Layer>(i);
        check(esc::indexOf(layer) >= 0, "every layer is registered");
        check(esc::layerName(layer) != nullptr &&
                  std::string(esc::layerName(layer)) != "unknown",
              "every layer has a stable name");
        check(esc::decide(Levels{}.with(layer)).layer == layer,
              "a single open layer answers itself");
        for (unsigned j = i + 1; j < static_cast<unsigned>(Layer::Count); ++j) {
            check(esc::indexOf(layer) != esc::indexOf(static_cast<Layer>(j)),
                  "layer positions are unique");
        }
    }

    // The whole stack open at once: the palette wins, nothing else reacts.
    Levels all;
    for (unsigned i = 1; i < static_cast<unsigned>(Layer::Count); ++i)
        all = all.with(static_cast<Layer>(i));
    check(esc::decide(all).layer == Layer::QuickJump, "all layers: quick jump wins");

    // Whatever is open, Esc answers an open layer or nothing. It never
    // resolves to a quit/close action: Action has no such value, and no layer
    // maps to it.
    for (unsigned bits = 1; bits < (1u << 12); ++bits) {
        Levels levels;
        for (unsigned i = 1; i < 13; ++i)
            if (bits & (1u << i)) levels = levels.with(static_cast<Layer>(i));
        esc::Decision decision = esc::decide(levels);
        check(decision.action != Action::None || decision.layer == Layer::None ||
                  decision.layer == Layer::Home,
              "only the Home layer may leave Esc unhandled");
        check(decision.layer == Layer::None || levels.open(decision.layer),
              "the winner must be an open layer");
        check(isTopmostOf(levels, decision.layer), "winner is the top-most layer");
    }

    if (failures == 0) std::cout << "escape_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
