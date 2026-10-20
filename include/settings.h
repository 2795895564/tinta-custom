#ifndef TINTA_SETTINGS_H
#define TINTA_SETTINGS_H

#include "app.h"

std::wstring getSettingsPath();
bool saveSettings(const Settings& settings);
// Save and, when the write fails, say so in the window instead of dropping
// the failure (invariant 7: a failed settings write must be visible).
bool saveSettingsReported(App& app, const Settings& settings);
Settings loadSettings();

// Reading position memory (#77): most-recent-first list capped in remember.
// The persist/lookup helpers load and save settings.ini themselves.
void rememberReadingPosition(Settings& settings, const std::string& path,
                             float scrollY, float zoom = 0.0f);
void persistReadingPosition(const std::string& path, float scrollY,
                            float zoom = 0.0f);
float findReadingPosition(const Settings& settings, const std::string& path);
// Per-document zoom, or 0 when the document has none stored
float findReadingZoom(const Settings& settings, const std::string& path);

// Start page recents ([Recent] section): record an open, or clear the list
void persistRecentFile(const std::string& path);
void clearRecentFiles();

// Theme changes persist immediately so new windows spawn in the same look
void persistThemeChoice(App& app);
// Editor mode pill choice (and the assists switch) persist immediately
void persistEditorMode(App& app);
void persistWordCount(App& app);
void persistEditorCentering(App& app);
void persistCloseKeepsDrafts(App& app);
// Print margins persist when picked in the preview (#257)
void persistPrintMargin(App& app);
void persistSearchScopes(App& app);
void persistOpenInTabs(App& app);
bool persistStartupMode(App& app);

// The configuration home: the exe's folder in portable mode (a
// settings.ini beside tinta.exe opts in, #147), else %APPDATA%\Tinta
std::wstring tintaConfigDir();

// Resolve the actual file before handing it to an external editor, which
// does not share a Store/MSIX process's virtualized AppData view.
std::wstring configFilePathForShell(const std::wstring& path);

// Resolve the selected profile and custom [Keys] bindings.
void applyKeymap(App& app, const Settings& settings);
bool registerFileAssociation();
void openDefaultAppsSettings();
void askAndRegisterFileAssociation(Settings& settings);

#endif // TINTA_SETTINGS_H
