#ifndef TINTA_SETTINGS_SCHEMA_H
#define TINTA_SETTINGS_SCHEMA_H

#include "app.h"
#include "settings_actions.h"

// The preference model, separate from any control. One row per setting that
// the Settings panel may show: the row says where it belongs, how it behaves,
// when it takes effect, which text it wears, what value a fresh install has,
// and for a toggle, which App field and which click it draws.
// `sanitize` walks the same rows so the legal range lives next to the setting
// instead of in a second, hand-written switch.
//
// Adding a boolean preference is therefore: one row here (with its panel text,
// `boolField` and `action`) + the side effect its click needs in input.cpp.
// The renderer has no table of its own.
namespace schema {

enum class Group {
    Appearance,
    Editor,
    Reading,
    Workspace,
    Keys,
    Files,
    Advanced,
};

enum class Type {
    Bool,
    Enum,
    Int,     // integer, optionally bounded (a percentage or a width)
    String,  // free text, such as a tool path
    Chord,   // a key binding: a virtual key plus Ctrl/Shift
};

enum class Apply {
    Live,       // takes effect in this window immediately
    NewWindow,  // the next window reads it (spawn/join decisions)
    Restart,    // read once at startup
};

// Where the value lives. Global settings are written once for every window;
// window settings are read by each window when it is created.
enum class Scope {
    Global,
    Window,
};

// Numeric bounds for an Int row. `bounded == false` means the row has no legal
// range of its own (a theme index, a language id) and sanitize leaves it alone.
struct Range {
    float min = 0.0f;
    float max = 0.0f;
    float fallback = 0.0f;
    bool bounded = false;
};

// The one place a row's value is made legal. A null validator means the value
// is not clamped (bools, enums, strings, unbounded ints).
using Validator = void (*)(Settings&);

// What a toggle does besides flipping its field. The renderer ignores these;
// input.cpp applies the bits after the flip. A new boolean setting that reuses
// existing behaviour is therefore one schema row, and only a genuinely new kind
// of follow-up work adds a bit here.
enum ToggleEffect : unsigned {
    FxNone = 0,
    FxLayoutDirty = 1u << 0,        // the page has to be laid out again
    FxClearLayoutCache = 1u << 1,   // editor line layouts are stale
    FxPreviewReset = 1u << 2,       // the docked preview follows the caret again
    FxPinCaret = 1u << 3,           // re-centre the caret while editing
    FxPersistEditorMode = 1u << 4,
    FxPersistWordCount = 1u << 5,
    FxPersistCloseDrafts = 1u << 6,
    FxPersistCentering = 1u << 7,
    FxPersistOpenInTabs = 1u << 8,
    FxThemeSync = 1u << 9,          // remember the theme each mode was on
    FxDocCountsStale = 1u << 10,    // word counts need one recount
    FxClearFolderSearch = 1u << 11, // results panel no longer includes the folder
};

struct Row {
    const char* id;  // stable, also the settings.ini key
    Group group;
    Type type;
    Apply apply;
    Scope scope;
    bool advanced;
    // Panel text; nullptr on rows whose control is still hand-written or whose
    // text comes from the keymap (the 13 chord rows).
    const char* labelKey;
    const char* hintKey;
    // Serialized value a fresh install has, e.g. "true", "100", "auto", "E".
    const char* defaultValue;
    Range range;
    Validator validate;
    // Toggle rows only: the App field the switch writes and the overlay action
    // its click reports. nullptr/SET_NONE on every other row.
    bool App::* boolField;
    SettingsAction action;
    // Toggle rows only: what has to follow the flip (see ToggleEffect).
    unsigned effects;
};

// Rows in panel order (the Keys group ends with one row per key action). The
// count is written to `count`.
const Row* rows(int& count);

// Row by id, or nullptr. Callers use this instead of duplicating the text.
const Row* find(const char* id);

// Row whose click answers `action`, or nullptr. Input uses this to flip a
// toggle's field without naming the field again.
const Row* findByAction(SettingsAction action);

const char* groupName(Group group);
const char* applyName(Apply apply);
const char* scopeName(Scope scope);
const char* typeName(Type type);

// Bounds of an Int row, or nullptr when it has none.
const Range* rangeOf(const char* id);

// The serialized default of a row, or nullptr for an unknown id.
const char* defaultOf(const char* id);

// Clamp every row that has a validator and replace non-finite numbers. Values
// already in range are left exactly as they are, so this is safe to run on
// every save.
void sanitize(Settings& settings);

}  // namespace schema

#endif
