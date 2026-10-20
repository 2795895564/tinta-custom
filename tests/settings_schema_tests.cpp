#include "settings_schema.h"

#include "keymap.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
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
    int count = 0;
    const schema::Row* table = schema::rows(count);
    check(table != nullptr, "the schema exposes its rows");
    check(count >= 12, "the panel has a real set of rows");

    bool groupSeen[7] = {false, false, false, false, false, false, false};
    for (int i = 0; i < count; ++i) {
        const schema::Row& row = table[i];
        check(row.id != nullptr && row.id[0] != '\0', "every row has an id");
        check(std::strcmp(schema::groupName(row.group), "unknown") != 0,
              "every row belongs to a group");
        check(std::strcmp(schema::applyName(row.apply), "unknown") != 0,
              "every row has an apply timing");
        groupSeen[static_cast<int>(row.group)] = true;
        for (int j = i + 1; j < count; ++j) {
            check(std::strcmp(row.id, table[j].id) != 0,
                  "setting ids are unique");
        }
    }
    for (bool seen : groupSeen) check(seen, "every group has at least one row");

    // Lookup is by id, and a row that carries panel text must carry both keys.
    check(schema::find("followSystemTheme") != nullptr, "find resolves a known id");
    check(schema::find("not.a.setting") == nullptr, "find returns null for unknown ids");
    for (int i = 0; i < count; ++i) {
        const schema::Row& row = table[i];
        if (row.labelKey) check(row.hintKey != nullptr, "label and hint come together");
        if (row.hintKey) check(row.labelKey != nullptr, "hint and label come together");
        if (row.type == schema::Type::Bool && row.labelKey)
            check(schema::find(row.id) == &row, "find returns the row itself");
    }

    // Sanitize clamps each bounded value; nothing here may invent a setting.
    Settings s;
    s.readingWidthPct = 5;
    s.zenWidthPct = 250;
    s.tocWidth = 10.0f;
    s.browserWidth = 9000.0f;
    s.printMarginMm = -3.0f;
    s.zoomFactor = 10.0f;
    s.editorCaretCenterRatio = 2.0f;
    schema::sanitize(s);
    check(s.readingWidthPct == 30, "reading width clamps up to 30%");
    check(s.zenWidthPct == 100, "zen width clamps down to 100%");
    check(s.tocWidth == 180.0f, "toc width clamps to its minimum");
    check(s.browserWidth == 4000.0f, "browser width clamps to its maximum");
    check(s.printMarginMm == 0.0f, "print margin clamps to zero");
    check(s.zoomFactor == 3.0f, "zoom clamps to 300%");
    check(s.editorCaretCenterRatio == 1.0f, "caret ratio clamps to 1");

    // In-range values survive untouched, so sanitize is safe on every save.
    Settings ok;
    ok.readingWidthPct = 77;
    ok.zenWidthPct = 60;
    ok.tocWidth = 300.0f;
    ok.browserWidth = 420.0f;
    ok.printMarginMm = 12.5f;
    ok.zoomFactor = 1.5f;
    ok.editorCaretCenterRatio = 0.42f;
    const Settings before = ok;
    schema::sanitize(ok);
    check(ok.readingWidthPct == before.readingWidthPct, "valid width is kept");
    check(ok.tocWidth == before.tocWidth, "valid toc width is kept");
    check(ok.printMarginMm == before.printMarginMm, "valid margin is kept");
    check(ok.zoomFactor == before.zoomFactor, "valid zoom is kept");
    check(ok.editorCaretCenterRatio == before.editorCaretCenterRatio,
          "valid caret ratio is kept");

    // A corrupted value must not reach settings.ini as NaN.
    Settings broken;
    broken.zoomFactor = std::nanf("");
    broken.tocWidth = std::nanf("");
    schema::sanitize(broken);
    check(broken.zoomFactor == 1.0f, "NaN zoom falls back to 100%");
    check(broken.tocWidth == 280.0f, "NaN width falls back to the default");

    // Every row now carries the constitution's fields: a scope, a known type
    // and a serialized default. The legal range lives in the row, not in
    // sanitize's body, and a fresh install's value is declared next to it.
    int keysRows = 0;
    bool sawWindow = false;
    bool sawGlobal = false;
    for (int i = 0; i < count; ++i) {
        const schema::Row& row = table[i];
        check(std::strcmp(schema::scopeName(row.scope), "unknown") != 0,
              "every row has a scope");
        sawWindow = sawWindow || row.scope == schema::Scope::Window;
        sawGlobal = sawGlobal || row.scope == schema::Scope::Global;
        check(std::strcmp(schema::typeName(row.type), "unknown") != 0,
              "every row has a known type");
        check(row.defaultValue != nullptr, "every row declares its default");
        if (row.type == schema::Type::Bool && row.labelKey) {
            check(row.boolField != nullptr, "a drawn toggle owns its App field");
            check(row.action != SET_NONE, "a drawn toggle owns its click");
        }
        if (row.type == schema::Type::Bool && row.defaultValue) {
            check(std::strcmp(row.defaultValue, "true") == 0 ||
                      std::strcmp(row.defaultValue, "false") == 0,
                  "a bool default is true or false");
        }
        if (row.type == schema::Type::Int && row.range.bounded && row.defaultValue) {
            const float value = std::strtof(row.defaultValue, nullptr);
            check(value >= row.range.min && value <= row.range.max,
                  "a bounded default sits inside its range");
        }
        if (row.group == schema::Group::Keys) ++keysRows;
    }
    check(keysRows == KEY_ACTION_COUNT + 1,
          "Keys group holds the profile plus one row per key action");
    check(sawGlobal && sawWindow, "both scopes are in use");

    const schema::Range* reading = schema::rangeOf("readingWidthPct");
    check(reading && reading->bounded && reading->min == 30.0f && reading->max == 100.0f,
          "reading width range is declared in its row");
    check(schema::rangeOf("themeIndex") == nullptr, "an unbounded row has no range");
    check(std::strcmp(schema::defaultOf("readingWidthPct"), "100") == 0,
          "the declared default matches the fresh-install value");
    check(schema::find("headingRules") && schema::find("headingRules")->scope == schema::Scope::Window,
          "a window preference says so");

    // String and chord rows exist, and the chord rows mirror the keymap rather
    // than repeating its values by hand.
    const schema::Row* pandocRow = schema::find("pandocPath");
    check(pandocRow && pandocRow->type == schema::Type::String, "tool paths are String rows");
    const schema::Row* editRow = schema::find("key.edit");
    check(editRow && editRow->type == schema::Type::Chord, "key actions are Chord rows");
    check(editRow && std::strcmp(editRow->defaultValue,
                                 keyIniName(KEY_PROFILES[keyProfileIndexById("windows")].keys[KA_EDIT]).c_str()) == 0,
          "a chord row's default comes from the Windows profile");

    // The toggle rows carry what the panel used to keep in a second table: the
    // App field and the click. Adding a drawn toggle is one row now.
    const schema::Row* wrapRow = schema::find("editorWordWrap");
    check(wrapRow && wrapRow->boolField == &App::editorWordWrap,
          "toggle row owns its App field");
    check(wrapRow && wrapRow->action == SET_TOGGLE_WRAP, "toggle row owns its action");
    int wrapRows = 0;
    for (int i = 0; i < count; ++i) {
        if (table[i].action == SET_TOGGLE_WRAP) ++wrapRows;
    }
    check(wrapRows == 1, "an action answers exactly one row");
    const schema::Row* pandocRowInfo = schema::find("pandocPath");
    check(pandocRowInfo && pandocRowInfo->boolField == nullptr,
          "a non-toggle row has no field");
    check(pandocRowInfo && pandocRowInfo->action == SET_LOCATE_PANDOC,
          "a path row owns its browse action");
    check(pandocRowInfo && pandocRowInfo->labelKey && pandocRowInfo->hintKey,
          "a drawn path row owns its text");
    check(pandocRowInfo && pandocRowInfo->effects == schema::FxNone,
          "a path row has no toggle follow-up");

    // The follow-up work is declared on the row, so input.cpp no longer needs a
    // case per setting. These four pin the mapping that used to live there.
    check(wrapRow && wrapRow->effects == schema::FxClearLayoutCache,
          "wrap clears the editor line cache");
    const schema::Row* previewRow = schema::find("editorShowPreview");
    check(previewRow && previewRow->effects ==
                            (schema::FxLayoutDirty | schema::FxPreviewReset),
          "preview relayouts and resets follow");
    const schema::Row* headingRow = schema::find("headingRules");
    check(headingRow && headingRow->effects == schema::FxLayoutDirty,
          "heading rules relayout");
    const schema::Row* followRow = schema::find("followSystemTheme");
    check(followRow && followRow->effects == schema::FxThemeSync,
          "follow-system theme remembers the mode's theme");
    const schema::Row* draftsRow = schema::find("closeKeepsDrafts");
    check(draftsRow && draftsRow->effects == schema::FxPersistCloseDrafts,
          "drafts preference persists on flip");

    // input.cpp now trusts the table for every toggle: an action without a row
    // would silently do nothing, so pin the full set.
    const SettingsAction toggles[] = {
        SET_TOGGLE_FOLLOW, SET_TOGGLE_FOLDERSEARCH, SET_TOGGLE_BROWSEFOCUS,
        SET_TOGGLE_OPENTABS, SET_TOGGLE_HEADRULES, SET_TOGGLE_WRAP,
        SET_TOGGLE_PREVIEW, SET_TOGGLE_CENTER_CARET, SET_TOGGLE_ASSISTS,
        SET_TOGGLE_WORDCOUNT, SET_TOGGLE_CLOSE_DRAFTS};
    for (SettingsAction action : toggles) {
        const schema::Row* found = nullptr;
        for (int i = 0; i < count; ++i) {
            if (table[i].action == action) {
                found = &table[i];
                break;
            }
        }
        check(found && found->boolField, "every toggle action has a row with a field");
    }

    if (failures == 0) std::cout << "settings_schema_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
