// Regression for D33: the Keyboard shortcuts section of the Settings panel.
//
// The panel and the input router both read the model in settings_keys.h, so
// these checks pin the contract between them: five sections in rail order, one
// generated row per key action, panel heights per section, and the capture
// rules (which keys are accepted, when a chord is refused as a conflict).

#include "settings_keys.h"

#include <iostream>

namespace {
int failures = 0;

void check(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}  // namespace

int main() {
    using namespace settingskeys;

    // The rail has five sections and the shortcuts page is the last one. The
    // rail labels in overlays.cpp are translations, but the order and the
    // actions come from this table, so a sixth section must start here.
    check(SectionCount == 5, "the rail has five sections");
    check(SectionKeys == 4, "keyboard shortcuts is the fifth section");
    for (int i = 0; i < SectionCount; ++i) {
        check(railAction(i) != SET_NONE, "every rail item reports an action");
        check(sectionForAction(railAction(i)) == i,
              "a rail action maps back to its section");
        check(railAction(i) != railAction((i + 1) % SectionCount),
              "rail actions are distinct");
    }
    check(sectionForAction(SET_TOGGLE_FOLLOW) == -1,
          "non-section actions do not select a section");
    check(railAction(-1) == SET_NONE && railAction(SectionCount) == SET_NONE,
          "rail lookups are range checked");

    // Height per section: Appearance and Keys were raised to 650 so their last
    // row clears the footer; General stays 590.
    check(panelHeight(SectionGeneral) == 590.0f, "general panel height");
    check(panelHeight(SectionAppearance) == 650.0f, "appearance panel height");
    check(panelHeight(SectionEditor) == 648.0f, "editor panel height");
    check(panelHeight(SectionFrontmatter) == 650.0f, "frontmatter panel height");
    check(panelHeight(SectionKeys) == 650.0f, "shortcuts panel height");

    // The page is generated from KEY_ACTIONS: every action once, in keymap
    // order. A new action added to the keymap appears here for free, and a
    // hand-written second list cannot fall out of sync.
    check(rowCount() == KEY_ACTION_COUNT, "one row per key action");
    check(KEY_ACTION_COUNT == 17, "the keymap lists 17 actions");
    for (int i = 0; i < KEY_ACTION_COUNT; ++i) {
        check(rowForAction(KEY_ACTIONS[i].name) == i,
              "every action resolves to its own row");
    }
    check(rowForAction("toc") == KA_TOC, "toc row");
    check(rowForAction("annotate") == KA_ANNOTATE, "annotate row");
    check(rowForAction("no-such-action") == -1, "unknown action has no row");
    check(rowForAction(nullptr) == -1, "null action has no row");

    // Grouping: the page reads as five short lists instead of one flat
    // column. Membership is by action name and the draw order is derived, so
    // reordering KEY_ACTIONS cannot silently regroup or duplicate a row.
    // Folder browser and TOC stay in the keymap (their fixed keys still work)
    // but are no longer offered for rebinding, so they have no group row.
    check(groupCount() == GroupCount && GroupCount == 5, "five shortcut groups");
    check(displayCount() == 15, "fifteen rebindable rows are displayed");
    int grouped = 0;
    for (int i = 0; i < KEY_ACTION_COUNT; ++i) {
        if (groupOfAction(i) >= 0) ++grouped;
    }
    check(grouped == displayCount(), "displayed rows match grouped actions");
    check(groupOfAction(rowForAction("browse")) == -1 &&
          groupOfAction(rowForAction("toc")) == -1,
          "folder browser and TOC are not offered for rebinding");
    bool shown[KEY_ACTION_COUNT] = {};
    int lastGroup = -1;
    for (int d = 0; d < displayCount(); ++d) {
        const int row = rowAtDisplay(d);
        check(row >= 0 && row < KEY_ACTION_COUNT, "display position has a row");
        if (row < 0 || row >= KEY_ACTION_COUNT) continue;
        check(!shown[row], "a row is displayed only once");
        shown[row] = true;
        const int group = groupOfAction(row);
        check(group >= 0 && group < GroupCount, "every row belongs to a group");
        check(group >= lastGroup, "groups stay in order and contiguous");
        lastGroup = group;
    }
    for (int i = 0; i < KEY_ACTION_COUNT; ++i) {
        check(shown[i] || groupOfAction(i) == -1,
              "every grouped action is displayed");
    }
    for (int g = 0; g < GroupCount; ++g) {
        check(groupTitleKey(g) != nullptr && groupTitleKey(g)[0] != '\0',
              "every group has a title key");
        bool hasRow = false;
        for (int i = 0; i < KEY_ACTION_COUNT; ++i) {
            if (groupOfAction(i) == g) hasRow = true;
        }
        check(hasRow, "every group has at least one action");
    }
    check(groupOfAction(rowForAction("quickjump")) == GroupNavigate,
          "quick jump is a navigation action");
    check(groupOfAction(rowForAction("zen")) == GroupView, "zen is a view action");
    check(groupOfAction(rowForAction("scrollup")) == GroupScroll,
          "scroll up is a scrolling action");
    check(groupOfAction(rowForAction("newfolder")) == GroupFile &&
          groupOfAction(rowForAction("rename")) == GroupFile &&
          groupOfAction(rowForAction("delete")) == GroupFile &&
          groupOfAction(rowForAction("quit")) == GroupFile,
          "file operations share the file group");
    check(groupOfAction(rowForAction("annotate")) == GroupEdit,
          "annotate is an editing action");
    check(groupOfAction(-1) == -1 && groupOfAction(KEY_ACTION_COUNT) == -1,
          "group lookup is range checked");
    check(groupTitleKey(-1) == nullptr && groupTitleKey(GroupCount) == nullptr,
          "group title lookup is range checked");
    check(rowAtDisplay(-1) == -1 && rowAtDisplay(displayCount()) == -1,
          "display lookup is range checked");

    // Capture key sets: key-down accepts navigation/function/letter/digit
    // keys, and rejects Esc (disarm), modifiers and layout-dependent keys.
    check(acceptsCaptureKey(VK_TAB) && acceptsCaptureKey(VK_SPACE),
          "tab and space are capturable");
    check(acceptsCaptureKey(VK_F1) && acceptsCaptureKey(VK_F12),
          "function keys are capturable");
    check(acceptsCaptureKey('A') && acceptsCaptureKey('Z') &&
          acceptsCaptureKey('0') && acceptsCaptureKey('9'),
          "letters and digits are capturable");
    check(!acceptsCaptureKey(VK_ESCAPE), "escape disarms instead of binding");
    check(!acceptsCaptureKey(VK_F13) && !acceptsCaptureKey(VK_BACK) &&
          !acceptsCaptureKey(VK_LEFT) && !acceptsCaptureKey(VK_OEM_1) &&
          !acceptsCaptureKey(VK_CONTROL),
          "unlisted keys are not capturable");
    check(acceptsCaptureChar('!') && acceptsCaptureChar('~') &&
          acceptsCaptureChar(':'),
          "printable ASCII arrives from WM_CHAR");
    check(!acceptsCaptureChar(' ') && !acceptsCaptureChar(127) &&
          !acceptsCaptureChar(0x00E9),
          "non-printable and non-ASCII characters are not capturable");

    // Capture rules: a free chord binds, re-confirming the row's own chord is
    // not a conflict, and a chord owned by another action in an overlapping
    // focus is refused. This is the rule the panel relies on to keep one chord
    // per focus.
    KeyBinding keys[KEY_ACTION_COUNT];
    resolveKeymap(keys, "windows", {});
    check(captureBinding(keys, KA_TOC, keys[KA_TOC]) == CaptureOutcome::Bound,
          "re-confirming a row's own key binds");
    check(captureBinding(keys, KA_TOC, keys[KA_BROWSE]) == CaptureOutcome::Conflict,
          "a chord owned by another action conflicts");
    check(captureBinding(keys, KA_TOC, parseKeyName("Ctrl+Shift+K")) ==
              CaptureOutcome::Bound,
          "an unused chord binds");
    check(captureBinding(keys, -1, parseKeyName("K")) == CaptureOutcome::Ignored,
          "no armed row ignores the binding");
    check(captureBinding(keys, KEY_ACTION_COUNT, parseKeyName("K")) ==
              CaptureOutcome::Ignored,
          "an out-of-range row ignores the binding");
    check(captureBinding(nullptr, KA_TOC, parseKeyName("K")) == CaptureOutcome::Ignored,
          "a missing keymap ignores the binding");
    check(captureBinding(keys, KA_TOC, KeyBinding{}) == CaptureOutcome::Ignored,
          "an empty binding is ignored");

    if (failures == 0) {
        std::cout << "settings_keys_tests: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
