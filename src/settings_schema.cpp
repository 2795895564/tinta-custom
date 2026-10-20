#include "settings_schema.h"

#include "keymap.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace schema {
namespace {

constexpr Range noRange() { return Range{}; }

constexpr Range boundedRange(float low, float high, float fallback) {
    return Range{low, high, fallback, true};
}

int clampInt(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

float clampFloat(float value, float low, float high, float fallback) {
    if (!std::isfinite(value)) return fallback;
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

// Each validator owns one setting's legal range. `sanitize` walks the table and
// calls them in row order, so a new bounded setting is one row plus one
// function, never a new branch in a central switch.
void validateReadingWidth(Settings& settings) {
    settings.readingWidthPct = clampInt(settings.readingWidthPct, 30, 100);
}

void validateZenWidth(Settings& settings) {
    settings.zenWidthPct = clampInt(settings.zenWidthPct, 30, 100);
}

void validatePrintMargin(Settings& settings) {
    settings.printMarginMm = clampFloat(settings.printMarginMm, 0.0f, 40.0f, 19.05f);
}

void validateZoom(Settings& settings) {
    settings.zoomFactor = clampFloat(settings.zoomFactor, 0.5f, 3.0f, 1.0f);
}

void validateCaretRatio(Settings& settings) {
    settings.editorCaretCenterRatio =
        clampFloat(settings.editorCaretCenterRatio, 0.0f, 1.0f, 0.5f);
}

// id, group, type, apply, scope, advanced, labelKey, hintKey, default, range,
// validate, boolField, action, effects.
constexpr Row kBaseRows[] = {
    // Appearance
    {"themeIndex", Group::Appearance, Type::Int, Apply::Live, Scope::Global, false,
     nullptr, nullptr, "5", noRange(), nullptr, nullptr, SET_NONE, FxNone},
    {"followSystemTheme", Group::Appearance, Type::Bool, Apply::Live, Scope::Global, false,
     "settings.follow_windows", "settings.follow_windows.hint", "false", noRange(), nullptr,
     &App::followSystemTheme, SET_TOGGLE_FOLLOW, FxThemeSync},
    {"headingRules", Group::Appearance, Type::Bool, Apply::Live, Scope::Window, false,
     "settings.heading_rules", "settings.heading_rules.hint", "true", noRange(), nullptr,
     &App::headingRules, SET_TOGGLE_HEADRULES, FxLayoutDirty},
    {"language", Group::Appearance, Type::Enum, Apply::Live, Scope::Global, false,
     nullptr, nullptr, "auto", noRange(), nullptr, nullptr, SET_NONE, FxNone},
    // Editor
    {"editorWordWrap", Group::Editor, Type::Bool, Apply::Live, Scope::Window, false,
     "settings.word_wrap", "settings.word_wrap.hint", "false", noRange(), nullptr,
     &App::editorWordWrap, SET_TOGGLE_WRAP, FxClearLayoutCache},
    {"editorShowPreview", Group::Editor, Type::Bool, Apply::Live, Scope::Window, false,
     "settings.preview_pane", "settings.preview_pane.hint", "true", noRange(), nullptr,
     &App::editorShowPreview, SET_TOGGLE_PREVIEW, FxLayoutDirty | FxPreviewReset},
    {"editorCenterCaret", Group::Editor, Type::Bool, Apply::Live, Scope::Window, true,
     "settings.center_caret", "settings.center_caret.hint", "true", noRange(), nullptr,
     &App::editorCenterCaret, SET_TOGGLE_CENTER_CARET, FxPersistCentering | FxPinCaret},
    {"editorCaretCenterRatio", Group::Editor, Type::Int, Apply::Live, Scope::Window, true,
     nullptr, nullptr, "0.5", boundedRange(0.0f, 1.0f, 0.5f), validateCaretRatio,
     nullptr, SET_NONE, FxNone},
    {"editorAssists", Group::Editor, Type::Bool, Apply::Live, Scope::Global, false,
     "settings.editor_assists", "settings.editor_assists.hint", "true", noRange(), nullptr,
     &App::editorAssists, SET_TOGGLE_ASSISTS, FxPersistEditorMode},
    {"showWordCount", Group::Editor, Type::Bool, Apply::Live, Scope::Window, false,
     "settings.word_count", "settings.word_count.hint", "true", noRange(), nullptr,
     &App::showWordCount, SET_TOGGLE_WORDCOUNT,
     FxPersistWordCount | FxDocCountsStale},
    // Reading
    {"readingWidthPct", Group::Reading, Type::Int, Apply::Live, Scope::Window, false,
     nullptr, nullptr, "100", boundedRange(30.0f, 100.0f, 100.0f), validateReadingWidth,
     nullptr, SET_NONE, FxNone},
    {"zenWidthPct", Group::Reading, Type::Int, Apply::Live, Scope::Window, false,
     nullptr, nullptr, "60", boundedRange(30.0f, 100.0f, 60.0f), validateZenWidth,
     nullptr, SET_NONE, FxNone},
    {"printMarginMm", Group::Reading, Type::Int, Apply::Live, Scope::Global, true,
     nullptr, nullptr, "19.05", boundedRange(0.0f, 40.0f, 19.05f), validatePrintMargin,
     nullptr, SET_NONE, FxNone},
    {"zoomFactor", Group::Reading, Type::Int, Apply::Live, Scope::Window, true,
     nullptr, nullptr, "1", boundedRange(0.5f, 3.0f, 1.0f), validateZoom,
     nullptr, SET_NONE, FxNone},
    // Workspace
    {"openInTabs", Group::Workspace, Type::Bool, Apply::NewWindow, Scope::Global, false,
     "settings.open_in_tabs", "settings.open_in_tabs.hint", "true", noRange(), nullptr,
     &App::openInTabs, SET_TOGGLE_OPENTABS, FxPersistOpenInTabs},
    {"folderSearchEnabled", Group::Workspace, Type::Bool, Apply::Live, Scope::Global, false,
     "settings.folder_search", "settings.folder_search.hint", "true", noRange(), nullptr,
     &App::folderSearchEnabled, SET_TOGGLE_FOLDERSEARCH, FxClearFolderSearch},
    {"browserFocusPath", Group::Workspace, Type::Bool, Apply::Live, Scope::Global, false,
     "settings.browse_focus_path", "settings.browse_focus_path.hint", "false", noRange(),
     nullptr, &App::browserFocusPath, SET_TOGGLE_BROWSEFOCUS, FxNone},
    // Keys
    {"keyProfile", Group::Keys, Type::Enum, Apply::Restart, Scope::Global, false,
     nullptr, nullptr, "windows", noRange(), nullptr, nullptr, SET_NONE, FxNone},
    // Files
    {"closeKeepsDrafts", Group::Files, Type::Bool, Apply::Live, Scope::Global, false,
     "settings.close_drafts", "settings.close_drafts.hint", "false", noRange(), nullptr,
     &App::closeKeepsDrafts, SET_TOGGLE_CLOSE_DRAFTS, FxPersistCloseDrafts},
    {"pandocPath", Group::Files, Type::String, Apply::Live, Scope::Global, true,
     "settings.pandoc", "settings.pandoc.hint", "", noRange(), nullptr, nullptr,
     SET_LOCATE_PANDOC, FxNone},
    {"plantumlPath", Group::Files, Type::String, Apply::Live, Scope::Global, true,
     "settings.plantuml", "settings.plantuml.hint", "", noRange(), nullptr, nullptr,
     SET_LOCATE_PLANTUML, FxNone},
    // Advanced
    {"checkUpdates", Group::Advanced, Type::Bool, Apply::Restart, Scope::Global, true,
     nullptr, nullptr, "true", noRange(), nullptr, nullptr, SET_NONE, FxNone},
};

constexpr int kBaseCount = static_cast<int>(sizeof(kBaseRows) / sizeof(kBaseRows[0]));

// The chord rows come from the keymap instead of a second hand-written list: one
// row per key action, its default read from the Windows profile. The two string
// vectors own the ids and defaults the rows point at.
struct Table {
    std::vector<Row> rows;
    std::vector<std::string> text;
};

Table& table() {
    static Table built = [] {
        Table out;
        out.rows.assign(std::begin(kBaseRows), std::end(kBaseRows));
        out.text.reserve(static_cast<size_t>(KEY_ACTION_COUNT) * 2);
        const int windows = keyProfileIndexById("windows");
        for (int i = 0; i < KEY_ACTION_COUNT; ++i) {
            const KeyBinding binding =
                windows >= 0
                    ? KEY_PROFILES[windows].keys[i]
                    : KeyBinding{KEY_ACTIONS[i].defaultKey, KEY_ACTIONS[i].isChar, 0u};
            out.text.push_back(std::string("key.") + KEY_ACTIONS[i].name);
            out.text.push_back(keyIniName(binding));
            Row row{};
            row.id = out.text[out.text.size() - 2].c_str();
            row.group = Group::Keys;
            row.type = Type::Chord;
            row.apply = Apply::Live;
            row.scope = Scope::Global;
            row.advanced = false;
            row.labelKey = nullptr;
            row.hintKey = nullptr;
            row.defaultValue = out.text.back().c_str();
            row.range = noRange();
            row.validate = nullptr;
            row.boolField = nullptr;
            row.action = SET_NONE;
            row.effects = FxNone;
            out.rows.push_back(row);
        }
        return out;
    }();
    return built;
}

}  // namespace

const Row* rows(int& count) {
    const Table& all = table();
    count = static_cast<int>(all.rows.size());
    return all.rows.data();
}

const Row* find(const char* id) {
    if (!id) return nullptr;
    int count = 0;
    const Row* table = rows(count);
    for (int i = 0; i < count; ++i) {
        if (std::strcmp(table[i].id, id) == 0) return &table[i];
    }
    return nullptr;
}

const Row* findByAction(SettingsAction action) {
    if (action == SET_NONE) return nullptr;
    int count = 0;
    const Row* table = rows(count);
    for (int i = 0; i < count; ++i) {
        if (table[i].action == action) return &table[i];
    }
    return nullptr;
}

const char* groupName(Group group) {
    switch (group) {
        case Group::Appearance: return "appearance";
        case Group::Editor: return "editor";
        case Group::Reading: return "reading";
        case Group::Workspace: return "workspace";
        case Group::Keys: return "keys";
        case Group::Files: return "files";
        case Group::Advanced: return "advanced";
    }
    return "unknown";
}

const char* applyName(Apply apply) {
    switch (apply) {
        case Apply::Live: return "live";
        case Apply::NewWindow: return "new-window";
        case Apply::Restart: return "restart";
    }
    return "unknown";
}

const char* scopeName(Scope scope) {
    switch (scope) {
        case Scope::Global: return "global";
        case Scope::Window: return "window";
    }
    return "unknown";
}

const char* typeName(Type type) {
    switch (type) {
        case Type::Bool: return "bool";
        case Type::Enum: return "enum";
        case Type::Int: return "int";
        case Type::String: return "string";
        case Type::Chord: return "chord";
    }
    return "unknown";
}

const Range* rangeOf(const char* id) {
    const Row* row = find(id);
    return row && row->range.bounded ? &row->range : nullptr;
}

const char* defaultOf(const char* id) {
    const Row* row = find(id);
    return row ? row->defaultValue : nullptr;
}

void sanitize(Settings& settings) {
    int count = 0;
    const Row* table = rows(count);
    for (int i = 0; i < count; ++i) {
        if (table[i].validate) table[i].validate(settings);
    }
}

}  // namespace schema
