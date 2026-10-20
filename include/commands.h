#ifndef TINTA_COMMANDS_H
#define TINTA_COMMANDS_H

// A chord is a virtual key plus the modifiers held with it, and it maps to one
// command. The table below is the single place that says which chord means
// what: handlers ask for the command instead of re-testing ctrl/shift
// themselves, so two features cannot quietly claim the same chord.
//
// Pure logic: no Win32, no App. Callers read the modifier state and build the
// Chord (see chordFor / shortcutModifiersAllowed).

namespace cmd {

enum Modifier : unsigned {
    Ctrl = 1u << 0,
    Shift = 1u << 1,
    Alt = 1u << 2,
};

struct Chord {
    unsigned vk = 0;
    unsigned mods = 0;

    constexpr bool operator==(const Chord& other) const {
        return vk == other.vk && mods == other.mods;
    }
};

enum class Command {
    None = 0,
    SearchPanel,  // Ctrl+Shift+F: search with every match beside the page
    SaveAs,       // Ctrl+Shift+S: save under a new name
    NewWindow,    // Ctrl+Shift+N: quick note in its own window
    PastePlain,   // Ctrl+Shift+V: paste without formatting
    ReadingView,  // Ctrl+Shift+E: full-width reading view
};

// Exact match on key and every modifier; None when no row matches.
Command resolve(const Chord& chord);

// Compatibility gate that used to be shortcutModifiersAllowed: Alt belongs to
// the keyboard layout, so it never reaches a handler; a Ctrl+Shift chord must
// be in the table; navigation keys keep their selection chords. Chords with
// at most one of Ctrl/Shift pass through as before.
bool permitted(const Chord& chord);

const char* commandId(Command command);

// Compile-time guard: two rows with the same chord would make the later
// command unreachable, so the table refuses to build.
bool chordsAreUnique();

}  // namespace cmd

#endif
