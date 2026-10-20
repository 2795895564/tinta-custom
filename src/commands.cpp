#include "commands.h"

namespace cmd {
namespace {

// One row per chord that is allowed through the modifier gate. Keep this list
// short and exact: a row here is the only way a Ctrl+Shift chord reaches a
// handler.
struct Entry {
    Chord chord;
    Command command;
};

constexpr Entry kCommands[] = {
    {{'F', Ctrl | Shift}, Command::SearchPanel},
    {{'S', Ctrl | Shift}, Command::SaveAs},
    {{'N', Ctrl | Shift}, Command::NewWindow},
    {{'V', Ctrl | Shift}, Command::PastePlain},
    {{'E', Ctrl | Shift}, Command::ReadingView},
};

constexpr int kCommandCount = static_cast<int>(sizeof(kCommands) / sizeof(kCommands[0]));

constexpr bool uniqueChords() {
    for (int i = 0; i < kCommandCount; ++i) {
        for (int j = i + 1; j < kCommandCount; ++j) {
            if (kCommands[i].chord == kCommands[j].chord) return false;
        }
    }
    return true;
}

static_assert(uniqueChords(), "two chords claim the same command");

// Selection and navigation chords stay valid with Ctrl+Shift, exactly as the
// old whitelist had them. They are not commands: nothing binds them, they only
// have to reach the text handlers.
bool isNavigationKey(unsigned vk) {
    switch (vk) {
        case 0x09:  // VK_TAB
        case 0x25: case 0x26: case 0x27: case 0x28:  // arrows
        case 0x24: case 0x23:                        // VK_HOME / VK_END
        case 0x21: case 0x22:                        // VK_PRIOR / VK_NEXT
            return true;
        default:
            return false;
    }
}

}  // namespace

Command resolve(const Chord& chord) {
    for (int i = 0; i < kCommandCount; ++i) {
        if (kCommands[i].chord == chord) return kCommands[i].command;
    }
    return Command::None;
}

bool permitted(const Chord& chord) {
    if (chord.mods & Alt) return false;
    const bool ctrl = (chord.mods & Ctrl) != 0;
    const bool shift = (chord.mods & Shift) != 0;
    if (!ctrl || !shift) return true;
    if (resolve(chord) != Command::None) return true;
    return isNavigationKey(chord.vk);
}

bool chordsAreUnique() { return uniqueChords(); }

const char* commandId(Command command) {
    switch (command) {
        case Command::None: return "none";
        case Command::SearchPanel: return "search.panel";
        case Command::SaveAs: return "file.saveAs";
        case Command::NewWindow: return "file.newWindow";
        case Command::PastePlain: return "edit.pastePlain";
        case Command::ReadingView: return "view.reading";
    }
    return "unknown";
}

}  // namespace cmd
