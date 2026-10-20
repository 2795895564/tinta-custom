#ifndef TINTA_SETTINGS_ACTIONS_H
#define TINTA_SETTINGS_ACTIONS_H

// Control identities of the Settings panel. Render stores (rect, action) pairs
// in app.settingsHits and handleMouseUp resolves them against these ids.
//
// The schema points at one of these per row, so a row can say which control it
// draws and which click it answers. The enum lives on its own here because both
// the preference model and the overlay need it, and neither should have to pull
// in the other.
enum SettingsAction {
    SET_NONE = 0,
    SET_SECTION_GENERAL, SET_SECTION_APPEARANCE, SET_SECTION_EDITOR, SET_SECTION_FRONTMATTER,
    SET_SECTION_KEYS,
    SET_TOGGLE_FOLLOW, SET_TOGGLE_FOLDERSEARCH, SET_TOGGLE_BROWSEFOCUS,
    SET_TOGGLE_OPENTABS, SET_TOGGLE_HEADRULES,
    SET_TOGGLE_WRAP, SET_TOGGLE_PREVIEW, SET_TOGGLE_CENTER_CARET,
    SET_TOGGLE_ASSISTS,
    SET_TOGGLE_WORDCOUNT, SET_TOGGLE_CLOSE_DRAFTS,
    SET_LOCATE_PANDOC,
    SET_LOCATE_PLANTUML,
    SET_SLIDER_READING, SET_SLIDER_ZEN, SET_SLIDER_CARET,
    SET_OPEN_THEMES, SET_NEW_THEME, SET_EDIT_THEME,
    SET_OPEN_INI, SET_OPEN_THEMES_INI,
    SET_LANG_DROPDOWN, SET_OPEN_LANGS_INI,
    SET_KEYS_DROPDOWN, SET_EDIT_KEYS,
    SET_CLOSE,
    SET_STARTUP_LAST, SET_STARTUP_PICKER,
    // Language picks encode as SET_LANG_PICK_BASE + i: 0 = Auto, then the
    // registry languages in order (the list is dynamic via languages.ini)
    SET_LANG_PICK_BASE = 1000,
    // Shortcut profile picks: SET_KEYS_PICK_BASE + profile index, with
    // KEY_PROFILE_COUNT meaning the custom [Keys] profile
    SET_KEYS_PICK_BASE = 2000,
};

#endif
