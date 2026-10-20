#ifndef TINTA_SETTINGS_KEYS_H
#define TINTA_SETTINGS_KEYS_H

#include "keymap.h"
#include "settings_actions.h"

// The Settings overlay is one rail of sections, and the Keyboard shortcuts
// section is one page of key actions. This header is the model the overlay and
// the input router share: rail order, panel height, the Keys page rows, and
// what a captured key does. It holds no App, no D2D and no window state, so the
// rules can be tested without the UI.
namespace settingskeys {

// Rail order; the value is also App::settingsSection, so a section name and its
// index cannot drift. Home is not a section: it lives on the left of the rail.
enum Section {
    SectionGeneral = 0,
    SectionAppearance,
    SectionEditor,
    SectionFrontmatter,
    SectionKeys,
    SectionCount,
};

// Panel height in DIPs before dpi() scaling. Appearance needs room for three
// sliders plus four cards; the Keys page lists one row per action.
float panelHeight(int section);

// Action the rail item for `section` reports, or SET_NONE out of range.
SettingsAction railAction(int section);

// Section an action activates, or -1 when it is not a section action.
int sectionForAction(SettingsAction action);

// One row per key action, in KEY_ACTIONS order: the page is generated from the
// keymap, not a second hand-written list.
int rowCount();
// Row for the action named `name`, or -1 when no action has that id.
int rowForAction(const char* name);

// Keys the capture accepts from key-down (Tab/Space/F1-F12/A-Z/0-9) and from
// WM_CHAR (printable ASCII). Both entry points use these, so the standalone
// editor and the Keys page cannot accept different key sets.
bool acceptsCaptureKey(unsigned vk);
bool acceptsCaptureChar(unsigned ch);

enum class CaptureOutcome {
    Ignored,   // no armed row, or no usable binding
    Bound,     // the row may take this binding
    Conflict,  // another action already answers this chord in the same focus
};

// What pressing `binding` while `row` is armed should do.
CaptureOutcome captureBinding(const KeyBinding* keys, int row, KeyBinding binding);

// The Keys page groups its rows by the job the action does, so thirteen keys
// read as five short lists instead of one flat column. Membership is by action
// name, not by row index: reordering KEY_ACTIONS cannot silently regroup them.
enum ShortcutGroup {
    GroupNavigate = 0,  // search, folder browse, contents
    GroupView,          // theme, stats, zen
    GroupScroll,        // up, down
    GroupFile,          // new file, quit
    GroupEdit,          // edit, help, annotate
    GroupCount,
};

int groupCount();
// Group that owns `row`, or -1 when the row is out of range.
int groupOfAction(int row);
// i18n key for the group header, or nullptr when the group is out of range.
const char* groupTitleKey(int group);
// Draw order: groups in the order above, actions in keymap order inside each
// group. `rowAtDisplay` maps a draw position to its keymap row; every row
// appears exactly once, and a group's rows stay contiguous.
int displayCount();
int rowAtDisplay(int index);

}  // namespace settingskeys

#endif
