#include "settings_keys.h"

#include <cstring>

namespace settingskeys {

namespace {

struct RailItem {
    SettingsAction action;
};

constexpr RailItem kRail[] = {
    {SET_SECTION_GENERAL},
    {SET_SECTION_APPEARANCE},
    {SET_SECTION_EDITOR},
    {SET_SECTION_FRONTMATTER},
    {SET_SECTION_KEYS},
};

static_assert(SectionCount == (int)(sizeof(kRail) / sizeof(kRail[0])),
              "the rail table must cover every section");

}  // namespace

float panelHeight(int section) {
    switch (section) {
        case SectionGeneral: return 590.0f;
        case SectionAppearance: return 650.0f;
        case SectionEditor: return 648.0f;
        case SectionFrontmatter: return 650.0f;
        case SectionKeys: return 650.0f;
        default: return 590.0f;
    }
}

SettingsAction railAction(int section) {
    if (section < 0 || section >= SectionCount) return SET_NONE;
    return kRail[section].action;
}

int sectionForAction(SettingsAction action) {
    for (int i = 0; i < SectionCount; ++i) {
        if (kRail[i].action == action) return i;
    }
    return -1;
}

int rowCount() { return KEY_ACTION_COUNT; }

int rowForAction(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < KEY_ACTION_COUNT; ++i) {
        if (std::strcmp(KEY_ACTIONS[i].name, name) == 0) return i;
    }
    return -1;
}

namespace {

const char* const kNavigateActions[] = {"search", "quickjump"};
const char* const kViewActions[] = {"theme", "stats", "zen"};
const char* const kScrollActions[] = {"scrollup", "scrolldown"};
const char* const kFileActions[] = {"newfile", "newfolder", "rename", "delete", "quit"};
const char* const kEditActions[] = {"edit", "help", "annotate"};

struct GroupDef {
    const char* titleKey;
    const char* const* actions;
    int count;
};

constexpr GroupDef kGroups[] = {
    {"shortcuts.group.navigate", kNavigateActions, 2},
    {"shortcuts.group.view", kViewActions, 3},
    {"shortcuts.group.scroll", kScrollActions, 2},
    {"shortcuts.group.file", kFileActions, 5},
    {"shortcuts.group.edit", kEditActions, 3},
};

static_assert(GroupCount == (int)(sizeof(kGroups) / sizeof(kGroups[0])),
              "every shortcut group needs a table entry");

}  // namespace

int groupCount() { return GroupCount; }

int groupOfAction(int row) {
    if (row < 0 || row >= KEY_ACTION_COUNT) return -1;
    for (int g = 0; g < GroupCount; ++g) {
        for (int i = 0; i < kGroups[g].count; ++i) {
            if (std::strcmp(KEY_ACTIONS[row].name, kGroups[g].actions[i]) == 0) return g;
        }
    }
    return -1;
}

const char* groupTitleKey(int group) {
    if (group < 0 || group >= GroupCount) return nullptr;
    return kGroups[group].titleKey;
}

int displayCount() {
    int count = 0;
    for (int g = 0; g < GroupCount; ++g) {
        for (int i = 0; i < kGroups[g].count; ++i) {
            if (rowForAction(kGroups[g].actions[i]) >= 0) ++count;
        }
    }
    return count;
}

int rowAtDisplay(int index) {
    if (index < 0) return -1;
    int seen = 0;
    for (int g = 0; g < GroupCount; ++g) {
        for (int i = 0; i < kGroups[g].count; ++i) {
            const int row = rowForAction(kGroups[g].actions[i]);
            if (row < 0) continue;
            if (seen == index) return row;
            ++seen;
        }
    }
    return -1;
}

bool acceptsCaptureKey(unsigned vk) {
    return vk == VK_TAB || vk == VK_SPACE || vk == VK_DELETE ||
           (vk >= VK_F1 && vk <= VK_F12) ||
           (vk >= 'A' && vk <= 'Z') ||
           (vk >= '0' && vk <= '9');
}

bool acceptsCaptureChar(unsigned ch) { return ch > 32 && ch < 127; }

CaptureOutcome captureBinding(const KeyBinding* keys, int row, KeyBinding binding) {
    if (!keys || row < 0 || row >= KEY_ACTION_COUNT || !binding.key) {
        return CaptureOutcome::Ignored;
    }
    if (keyBindingConflict(keys, row, binding)) return CaptureOutcome::Conflict;
    return CaptureOutcome::Bound;
}

}  // namespace settingskeys
