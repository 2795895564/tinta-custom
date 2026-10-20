#include "keymap.h"
#include "commands.h"

#include <cctype>

// Compatibility shim: the chord table decides which Ctrl+Shift chords may
// reach a handler. Call sites that already ask for a Command do not need this.
bool shortcutModifiersAllowed(unsigned key) {
    unsigned mods = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= cmd::Ctrl;
    if (GetKeyState(VK_SHIFT) & 0x8000) mods |= cmd::Shift;
    if (GetKeyState(VK_MENU) & 0x8000) mods |= cmd::Alt;
    return cmd::permitted({key, mods});
}

namespace {

std::string lowerAscii(const std::string& value) {
    std::string out;
    for (unsigned char ch : value) out += (char)std::tolower(ch);
    return out;
}

// Splits "ctrl+shift+k" into modifier bits plus the bare key name.
unsigned takeModifierPrefix(std::string& lower) {
    unsigned mods = 0;
    for (;;) {
        if (lower.rfind("ctrl+", 0) == 0) {
            mods |= kModCtrl;
            lower.erase(0, 5);
        } else if (lower.rfind("shift+", 0) == 0) {
            mods |= kModShift;
            lower.erase(0, 6);
        } else {
            return mods;
        }
    }
}

std::string modifierPrefix(unsigned mods) {
    std::string out;
    if (mods & kModCtrl) out += "Ctrl+";
    if (mods & kModShift) out += "Shift+";
    return out;
}

}  // namespace

KeyBinding parseKeyName(const std::string& value) {
    if (value.empty()) return {};
    std::string lower = lowerAscii(value);
    unsigned mods = takeModifierPrefix(lower);
    if (lower.empty()) return {};
    if (lower.size() == 1) {
        unsigned char ch = (unsigned char)lower[0];
        if (ch < 32 || ch >= 127) return {};
        // Preserve the character's case in the key: WM_CHAR matching is
        // case-sensitive for punctuation, and letters are compared as VKs.
        unsigned key = (unsigned)std::toupper(ch);
        bool isChar = ch != ' ' && !std::isalnum(ch);
        return {key, isChar, mods};
    }
    if (lower == "tab") return {VK_TAB, false, mods};
    if (lower == "space") return {VK_SPACE, false, mods};
    if (lower == "del" || lower == "delete") return {VK_DELETE, false, mods};
    for (unsigned n = 1; n <= 12; ++n) {
        if (lower == "f" + std::to_string(n)) return {VK_F1 + n - 1, false, mods};
    }
    return {};
}

std::string keyIniName(KeyBinding binding) {
    std::string base;
    if (!binding.isChar) {
        if (binding.key == VK_TAB) base = "Tab";
        else if (binding.key == VK_SPACE) base = "Space";
        else if (binding.key == VK_DELETE) base = "Del";
        else if (binding.key >= VK_F1 && binding.key <= VK_F12)
            base = "F" + std::to_string(binding.key - VK_F1 + 1);
    }
    if (base.empty()) base = std::string(1, (char)binding.key);
    return modifierPrefix(binding.mods) + base;
}

std::wstring keyLabel(KeyBinding binding) {
    std::string value = keyIniName(binding);
    return std::wstring(value.begin(), value.end());
}

int keyProfileIndexById(const std::string& id) {
    for (int i = 0; i < KEY_PROFILE_COUNT; ++i)
        if (id == KEY_PROFILES[i].id) return i;
    return -1;
}

void resolveKeymap(KeyBinding (&keys)[KEY_ACTION_COUNT], const std::string& profile,
                   const std::vector<std::pair<std::string, std::string>>& overrides) {
    int index = keyProfileIndexById(profile);
    // A custom map (or a stale profile name) starts from the Windows set, so
    // "Reset all" lands on the bindings a fresh install shows. The bare table
    // values (':', '?') are command codes, not keys.
    const bool custom = index < 0;
    const int base = custom ? keyProfileIndexById("windows") : index;
    for (int i = 0; i < KEY_ACTION_COUNT; ++i) {
        keys[i] = base >= 0 ? KEY_PROFILES[base].keys[i]
                            : KeyBinding{KEY_ACTIONS[i].defaultKey, KEY_ACTIONS[i].isChar};
        if (!custom) continue;
        for (const auto& override : overrides) {
            if (override.first != KEY_ACTIONS[i].name) continue;
            KeyBinding binding = parseKeyName(override.second);
            if (binding.key) keys[i] = binding;
        }
    }
}

bool keyBindingMatches(KeyBinding binding, unsigned key, bool isChar, unsigned mods) {
    // Only punctuation bindings use WM_CHAR. Never case-fold a virtual
    // key as Unicode, or compare it to a layout-dependent character.
    if (isChar && key == 0xFF1A) key = ':';
    if (isChar && key == 0xFF1F) key = '?';
    return binding.isChar == isChar && binding.key == key && binding.mods == mods;
}

unsigned translateActionKey(const KeyBinding* keys, unsigned key, bool isChar,
                            unsigned mods) {
    for (int i = 0; i < KEY_ACTION_COUNT; ++i) {
        if (keyBindingMatches(keys[i], key, isChar, mods)) return KEY_ACTIONS[i].defaultKey;
    }
    if (isChar) {
        // Preserve the documented ':'/'?' fallbacks in every profile.
        if (key == ':' || key == 0xFF1A) return ':';
        if (key == '?' || key == 0xFF1F) return '?';
        return 0;
    }
    for (const auto& action : KEY_ACTIONS) {
        if (!action.isChar && action.defaultKey == key) return 0;
    }
    return key;
}

bool keyContextsOverlap(unsigned a, unsigned b) {
    return (a & b) != 0;
}

unsigned keyActionContexts(int row) {
    if (row < 0 || row >= KEY_ACTION_COUNT) return 0;
    return KEY_ACTIONS[row].contexts;
}

bool keyBindingConflict(const KeyBinding* keys, int row, KeyBinding binding) {
    // A row of -1 means "not assigned yet": there is no context to compare, so
    // every action is a candidate conflict.
    const unsigned mine = keyActionContexts(row);
    for (int i = 0; i < KEY_ACTION_COUNT; ++i) {
        if (i == row) continue;
        if (row >= 0 && !keyContextsOverlap(mine, KEY_ACTIONS[i].contexts)) continue;
        if (keys[i].key == binding.key && keys[i].isChar == binding.isChar &&
            keys[i].mods == binding.mods) {
            return true;
        }
    }
    return false;
}

bool keymapHasChord(const KeyBinding* keys, unsigned key, bool isChar, unsigned mods) {
    for (int i = 0; i < KEY_ACTION_COUNT; ++i) {
        if (keys[i].mods == mods && keys[i].isChar == isChar && keys[i].key == key)
            return true;
    }
    return false;
}
