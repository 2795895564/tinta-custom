#pragma once
// Home view: the repo file manager that takes the start page's slot when a
// repo root is known. Shell around HomeState (home_state.h): feeds it input
// and scan results, runs its side effects, and paints it with Direct2D.
#include "app.h"

#define WM_APP_DIR_SCAN (WM_APP + 8)  // lParam = ScanMsg*, ownership transfers

// Home takes over exactly where the launcher would draw
bool homeActive(const App& app);
// True when startup settings should skip session restore and show Home/picker
bool homeStartupWantsWindow(const Settings& settings);
// Decides once what the launcher slot shows: the last repo, or the repo picker
void homeEnsureRoot(App& app);

void renderHome(App& app);
bool homeOnKeyDown(App& app, HWND hwnd, WPARAM vk);       // true = consumed
bool homeOnChar(App& app, HWND hwnd, wchar_t c);          // true = consumed
bool homeOnMouseMove(App& app, HWND hwnd, int x, int y);  // true = hover changed
bool homeOnLButtonDown(App& app, HWND hwnd, int x, int y);
void homeOnWheel(App& app, HWND hwnd, float notches);
// Flush pending Home scan/open effects after a programmatic navigation.
void homeRefresh(App& app, HWND hwnd);
// Anchor the IME composition/candidate window at the Home dialog caret.
// Returns false when no text dialog is open.
bool homePositionImeWindow(App& app, HWND hwnd);
// Consume an IME composition result into the Home dialog's name field.
// Returns false when there is no Home name dialog to receive it.
bool homeImeComposition(App& app, HWND hwnd, LPARAM lParam);
bool homeOnDropFolder(App& app, HWND hwnd, const std::wstring& path);  // true = added as a repo
void homeOnScanMessage(App& app, HWND hwnd, LPARAM lParam);
