#ifndef TINTA_KEYMAP_H
#define TINTA_KEYMAP_H

#include <windows.h>
#include <string>
#include <utility>
#include <vector>

// Modifier bits carried by a binding. Alt stays out: it belongs to the
// keyboard layout and never reaches a handler (see shortcutModifiersAllowed).
inline constexpr unsigned kModCtrl = 1u << 0;
inline constexpr unsigned kModShift = 1u << 1;

// A virtual key and a typed character are different namespaces. In
// particular, VK_F1 is also the Unicode value of 'p', and VK_F12 of '{'.
// `mods` is a chord (Ctrl/Shift held with the key); 0 is a plain key, which
// is what every stock profile uses.
struct KeyBinding {
    unsigned key = 0;
    bool isChar = false;
    unsigned mods = 0;
};

// The focus a binding can answer in, as bit flags. Two actions may share a
// chord only when their contexts never coexist: "K" on Home and "K" in the
// outline is fine, "K" twice in the reader is not.
enum KeyContext : unsigned {
    CtxHome = 1u << 0,
    CtxReader = 1u << 1,
    CtxEditor = 1u << 2,
};

struct KeyActionDef {
    const char* name;
    unsigned defaultKey;
    bool isChar;
    unsigned contexts;
};
inline constexpr KeyActionDef KEY_ACTIONS[] = {
    {"search", 'F', false, CtxHome | CtxReader},
    {"browse", 'B', false, CtxHome | CtxReader},
    {"toc", VK_TAB, false, CtxHome | CtxReader},
    {"theme", 'T', false, CtxHome | CtxReader},
    {"stats", 'S', false, CtxHome | CtxReader},
    {"quit", 'Q', false, CtxHome | CtxReader},
    {"newfile", 'N', false, CtxHome | CtxReader},
    {"zen", VK_F11, false, CtxHome | CtxReader | CtxEditor},
    {"scrollup", 'K', false, CtxHome | CtxReader | CtxEditor},
    {"scrolldown", 'J', false, CtxHome | CtxReader | CtxEditor},
    {"edit", ':', true, CtxReader},
    {"help", '?', true, CtxHome | CtxReader | CtxEditor},
    {"annotate", 'A', false, CtxReader},
    // Palette and Home file operations. They live in the same table so they
    // can be rebound and conflict-checked like every other action.
    {"quickjump", 'K', false, CtxHome | CtxReader | CtxEditor},
    {"newfolder", 'N', false, CtxHome},
    {"rename", VK_F2, false, CtxHome},
    {"delete", 'D', false, CtxHome},
};
inline constexpr int KEY_ACTION_COUNT = 17;
enum KeyActionIndex {
    KA_SEARCH = 0, KA_BROWSE, KA_TOC, KA_THEME, KA_STATS, KA_QUIT,
    KA_NEWFILE, KA_ZEN, KA_SCROLLUP, KA_SCROLLDOWN, KA_EDIT, KA_HELP,
    KA_ANNOTATE, KA_QUICKJUMP, KA_NEWFOLDER, KA_RENAME, KA_DELETE
};

struct KeyProfileDef {
    const char* id;
    KeyBinding keys[KEY_ACTION_COUNT];
};
inline constexpr KeyProfileDef KEY_PROFILES[] = {
    {"windows", {{'F'}, {'B'}, {VK_TAB}, {'T'}, {'S'}, {'Q'}, {'N'},
                 {VK_F11}, {'K'}, {'J'}, {'E'}, {VK_F1}, {'A'},
                 {'K', false, kModCtrl}, {'N', false, kModShift},
                 {VK_F2}, {'D'}}},
    {"vim", {{'/', true}, {'B'}, {VK_TAB}, {'T'}, {'S'}, {'Q'}, {'N'},
             {VK_F11}, {'K'}, {'J'}, {':', true}, {'?', true}, {'A'},
             {'K', false, kModCtrl}, {'N', false, kModShift},
             {VK_F2}, {'D'}}},
};
inline constexpr int KEY_PROFILE_COUNT = 2;

int keyProfileIndexById(const std::string& id);
// Accepts "K", "Tab", "F1", ":", and chords such as "Ctrl+K", "Ctrl+Shift+F".
KeyBinding parseKeyName(const std::string& value);
std::string keyIniName(KeyBinding binding);
std::wstring keyLabel(KeyBinding binding);
void resolveKeymap(KeyBinding (&keys)[KEY_ACTION_COUNT], const std::string& profile,
                   const std::vector<std::pair<std::string, std::string>>& overrides);
bool keyBindingMatches(KeyBinding binding, unsigned key, bool isChar, unsigned mods = 0);
// True when at least one focus is common to both contexts, so a chord bound to
// both actions could be reached in the same state.
bool keyContextsOverlap(unsigned a, unsigned b);
// Contexts of the action in `row`, or 0 for an out-of-range row.
unsigned keyActionContexts(int row);
// Alt/AltGr belongs to the keyboard layout. Shift is accepted only for
// explicitly supported Ctrl commands and selection/navigation chords.
bool shortcutModifiersAllowed(unsigned key);
// Convert to the legacy viewer command codes. Unbound characters produce
// no command; navigation virtual keys pass through unless explicitly moved.
unsigned translateActionKey(const KeyBinding* keys, unsigned key, bool isChar,
                            unsigned mods = 0);
// True when `binding` already belongs to another action in `keys`. The row
// being edited is skipped so re-confirming the same binding is not a conflict.
bool keyBindingConflict(const KeyBinding* keys, int row, KeyBinding binding);
// True when any action in `keys` is bound to exactly this chord.
bool keymapHasChord(const KeyBinding* keys, unsigned key, bool isChar, unsigned mods);

#endif
