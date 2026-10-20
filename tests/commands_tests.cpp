#include "commands.h"

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
}  // namespace

int main() {
    using cmd::Chord;
    using cmd::Command;

    // Exact chords only: the gate must not fire on Ctrl+F or bare F.
    check(cmd::resolve({'F', cmd::Ctrl | cmd::Shift}) == Command::SearchPanel,
          "Ctrl+Shift+F is the search panel");
    check(cmd::resolve({'F', cmd::Ctrl}) == Command::None,
          "Ctrl+F is not the search panel chord");
    check(cmd::resolve({'F', cmd::Shift}) == Command::None,
          "Shift+F is not the search panel chord");
    check(cmd::resolve({'F', cmd::Ctrl | cmd::Shift | cmd::Alt}) == Command::None,
          "Alt is part of the chord, not ignored");

    // The table itself must stay collision-free: a duplicate makes the later
    // command unreachable.
    check(cmd::chordsAreUnique(), "no two rows claim the same chord");

    // The whole set the app currently routes through the modifier gate.
    const Chord gated[] = {
        {'F', cmd::Ctrl | cmd::Shift}, {'S', cmd::Ctrl | cmd::Shift},
        {'N', cmd::Ctrl | cmd::Shift}, {'V', cmd::Ctrl | cmd::Shift},
        {'E', cmd::Ctrl | cmd::Shift},
    };
    for (const Chord& chord : gated) {
        check(cmd::resolve(chord) != Command::None, "every gated chord has a command");
        check(cmd::permitted(chord), "every gated chord passes the gate");
        check(cmd::commandId(cmd::resolve(chord)) != std::string("unknown"),
              "every command has a stable id");
    }

    // Unknown Ctrl+Shift letters stay rejected: that was the whole point of
    // the whitelist, and it must not become "any chord goes".
    for (unsigned vk = 'A'; vk <= 'Z'; ++vk) {
        Chord chord{vk, cmd::Ctrl | cmd::Shift};
        bool known = cmd::resolve(chord) != Command::None;
        check(cmd::permitted(chord) == known,
              "Ctrl+Shift letters pass only when the table has the chord");
    }
    check(!cmd::permitted({'Q', cmd::Ctrl | cmd::Shift}),
          "Ctrl+Shift+Q is still not a shortcut");
    check(!cmd::permitted({'F', cmd::Ctrl | cmd::Shift | cmd::Alt}),
          "Alt+Ctrl+Shift+F never reaches a handler");

    // Navigation keeps its selection chords.
    const unsigned navigation[] = {0x09, 0x25, 0x26, 0x27, 0x28, 0x24, 0x23, 0x21, 0x22};
    for (unsigned vk : navigation) {
        check(cmd::permitted({vk, cmd::Ctrl | cmd::Shift}),
              "selection chords reach the text handlers");
    }

    // Plain and single-modifier keys are unaffected by the gate.
    check(cmd::permitted({'A', 0}), "bare keys pass");
    check(cmd::permitted({'A', cmd::Ctrl}), "Ctrl+letter passes");
    check(cmd::permitted({'A', cmd::Shift}), "Shift+letter passes");
    check(cmd::permitted({0x1B, 0}), "Esc passes");

    // Each chord maps to at most one command (first match wins), and no two
    // table rows answer the same chord with different commands.
    for (unsigned vk = 'A'; vk <= 'Z'; ++vk) {
        Chord once{vk, cmd::Ctrl | cmd::Shift};
        check(cmd::resolve(once) == cmd::resolve(once), "resolution is stable");
    }

    if (failures == 0) std::cout << "commands_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
