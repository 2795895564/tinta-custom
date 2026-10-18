// Home view shell: threads, input routing and Direct2D painting.
// The navigation rules live in home_logic.cpp and are unit-tested there.

#include "homeview.h"

#include <dwrite_1.h>
#include <shobjidl.h>

#include <algorithm>
#include <memory>
#include <thread>

#include "d2d_init.h"
#include "document.h"
#include "editor.h"  // toUtf8
#include "file_utils.h"
#include "i18n.h"
#include "repos.h"
#include "settings.h"
#include "startpage.h"
#include "tabs.h"
#include "utils.h"

namespace {

using dirmodel::Entry;
using dirmodel::Kind;

// --- Scanning --------------------------------------------------------------

struct ScanMsg {
    int slot;
    uint32_t gen;
    std::wstring path;
    dirmodel::Listing listing;
    std::wstring text;
};

bool isDocName(const std::wstring& name) {
    return isSupportedDocumentPath(std::wstring_view(name));
}

// Runs whatever the logic asked for. Each worker posts one ScanMsg back; a
// result whose generation went stale while it ran is dropped by the logic.
void dispatchScans(App& app, HWND hwnd) {
    HomeState& H = app.home;
    unsigned need = homeTakeRequests(H);
    for (int slot = 0; slot < 4; slot++) {
        if (!(need & (1u << slot))) continue;
        std::wstring path = slot == kHomeParent    ? dirmodel::parentOf(H.dir)
                            : slot == kHomeCurrent ? H.dir
                                                   : H.previewTarget;
        if (path.empty()) continue;
        uint32_t gen = H.gen[slot];
        std::thread([hwnd, slot, gen, path] {
            auto* msg = new ScanMsg{slot, gen, path, {}, {}};
            if (slot == kHomePreviewFile) msg->text = dirmodel::readHead(path, 8192);
            else dirmodel::scan(path, isDocName, msg->listing);
            if (!PostMessageW(hwnd, WM_APP_DIR_SCAN, 0, (LPARAM)msg)) delete msg;
        }).detach();
    }
}

// Same hand-over as the launcher's recent rows: open into a new tab, then
// close the empty shell tab the launcher lived in
void openFromHome(App& app, HWND hwnd, const std::wstring& path) {
    tabsInit(app);
    int shellId = app.tabs[app.activeTab].id;
    tabOpenPath(app, hwnd, toUtf8(path));
    for (int i = 0; i < (int)app.tabs.size(); i++) {
        if (app.tabs[i].id == shellId) { tabCloseIndex(app, hwnd, i); break; }
    }
}

// --- Repositories ----------------------------------------------------------

ULONGLONG nowTicks() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

bool dirExists(const std::string& utf8Path) {
    DWORD a = GetFileAttributesW(toWide(utf8Path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void refreshRows(App& app) {
    Settings st = loadSettings();
    std::vector<RepoRow> rows;
    for (const auto& r : st.repoList)
        rows.push_back({r.id, toWide(r.name), toWide(r.path), r.lastOpened, !dirExists(r.path)});
    homeSetRepoRows(app.home, std::move(rows));
}

// Remember where we were inside the repo, for the next launch
void persistLastDir(App& app) {
    HomeState& H = app.home;
    if (H.repoId.empty() || H.dir.empty()) return;
    Settings st = loadSettings();
    int i = repos::indexOfId(st.repoList, H.repoId);
    if (i < 0) return;
    st.repoList[i].lastDir = toUtf8(H.dir);
    saveSettings(st);
}

bool activateRepo(App& app, const Settings& st, const std::string& id) {
    HomeState& H = app.home;
    int i = repos::indexOfId(st.repoList, id);
    if (i < 0) return false;
    const repos::Repo& repo = st.repoList[i];
    if (!dirExists(repo.path)) {
        H.repoNotice = L"Folder not found: " + toWide(repo.path);
        refreshRows(app);
        return false;
    }
    H.repoId = id;
    H.screen = HomeScreen::Home;
    H.repoNotice.clear();
    homeSetRoot(H, toWide(repo.path),
                dirExists(repo.lastDir) ? toWide(repo.lastDir) : std::wstring());
    H.columns.clear();
    H.columnsRoot = H.root;
    H.hoverColumn = -1;
    H.lastClickColumn = -1;
    return true;
}

void enterRepo(App& app, const std::string& id) {
    HomeState& H = app.home;
    Settings st = loadSettings();
    int i = repos::indexOfId(st.repoList, id);
    if (i < 0) return;
    repos::Repo repo = st.repoList[i];
    if (!dirExists(repo.path)) {
        H.repoNotice = L"Folder not found: " + toWide(repo.path);
        refreshRows(app);
        return;
    }
    repos::touch(st.repoList, id, nowTicks());
    st.currentRepoId = id;
    saveSettings(st);
    activateRepo(app, st, id);
}

void addRepoFromFolder(App& app, const std::wstring& folder) {
    Settings st = loadSettings();
    std::string id = repos::add(st.repoList, toUtf8(folder), nowTicks());
    st.currentRepoId = id;
    bool saved = saveSettings(st);
    if (!activateRepo(app, st, id)) return;
    if (!saved)
        app.home.repoNotice =
            L"Repository added for this session, but settings could not be saved: " +
            getSettingsPath();
}

// Forgets the entry only; the folder on disk is never touched
void forgetRepo(App& app, const std::string& id) {
    HomeState& H = app.home;
    Settings st = loadSettings();
    repos::remove(st.repoList, id);
    if (st.currentRepoId == id) st.currentRepoId.clear();
    saveSettings(st);
    if (H.repoId == id) {
        H.repoId.clear();
        H.root.clear();
        H.dir.clear();
        H.current = dirmodel::Listing{};
        H.parent = dirmodel::Listing{};
        H.view.clear();
    }
    refreshRows(app);
}

std::wstring pickFolder(HWND owner) {
    std::wstring result;
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileOpenDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dlg)))) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dlg->SetTitle(L"Choose a repository folder");
        if (SUCCEEDED(dlg->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR p = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                    result = p;
                    CoTaskMemFree(p);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (init == S_OK) CoUninitialize();  // S_FALSE: COM was already initialized elsewhere
    return result;
}

// After any state change: run the side effects the logic left behind
// (dialogs, settings, scans, opening a file), then repaint
void settle(App& app, HWND hwnd) {
    HomeState& H = app.home;
    if (H.refreshRows) {
        H.refreshRows = false;
        persistLastDir(app);  // leaving the repo: keep the current folder
        refreshRows(app);
    }
    if (H.pendingBrowse) {
        H.pendingBrowse = false;
        std::wstring folder = pickFolder(hwnd);
        if (!folder.empty()) addRepoFromFolder(app, folder);
    }
    if (!H.pendingChoose.empty()) {
        std::string id = std::move(H.pendingChoose);
        H.pendingChoose.clear();
        enterRepo(app, id);
    }
    if (!H.pendingRemove.empty()) {
        std::string id = std::move(H.pendingRemove);
        H.pendingRemove.clear();
        forgetRepo(app, id);
    }
    dispatchScans(app, hwnd);
    if (!H.pendingOpen.empty()) {
        std::wstring path = std::move(H.pendingOpen);
        H.pendingOpen.clear();
        persistLastDir(app);
        openFromHome(app, hwnd, path);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- Home B column browser -------------------------------------------------

void saveActiveColumn(HomeState& H) {
    if (H.dir.empty()) return;
    HomeColumn c;
    c.path = H.dir;
    c.listing = H.current;
    c.view = H.view;
    c.sel = H.sel;
    c.topRow = H.topRow;
    H.columns.push_back(std::move(c));
    if (H.columns.size() > 2) H.columns.erase(H.columns.begin());
}

void restoreColumn(HomeState& H, int index) {
    if (index < 0 || index >= (int)H.columns.size()) return;
    HomeColumn c = std::move(H.columns[index]);
    H.columns.erase(H.columns.begin() + index, H.columns.end());
    H.dir = std::move(c.path);
    H.current = std::move(c.listing);
    H.view = std::move(c.view);
    H.sel = c.sel;
    H.topRow = c.topRow;
    H.query.clear();
    H.searching = false;
    H.loading = false;
}

bool openColumn(App& app, const std::wstring& path) {
    HomeState& H = app.home;
    if (H.root.empty() || !dirmodel::isInside(H.root, path)) return false;
    saveActiveColumn(H);
    dirmodel::Listing listing;
    dirmodel::scan(path, isDocName, listing);
    if (listing.ok) dirmodel::sortEntries(listing.entries, H.opts);
    H.dir = path;
    H.current = std::move(listing);
    H.query.clear();
    H.searching = false;
    H.loading = false;
    H.sel = 0;
    H.topRow = 0;
    H.view = dirmodel::visible(H.current.entries, H.opts, H.query);
    return true;
}

bool closeColumn(App& app) {
    HomeState& H = app.home;
    if (!H.columns.empty()) {
        restoreColumn(H, (int)H.columns.size() - 1);
        return true;
    }
    if (!H.dir.empty() && H.dir != H.root) {
        homeUp(H);
        return true;
    }
    return false;
}

bool openSelectedInColumn(App& app) {
    HomeState& H = app.home;
    const Entry* e = homeSelected(H);
    if (!e) return false;
    if (e->kind == Kind::Folder) return openColumn(app, dirmodel::join(H.dir, e->name));
    if (e->kind == Kind::Document) H.pendingOpen = dirmodel::join(H.dir, e->name);
    return true;
}


D2D1_COLOR_F mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                        a.b + (b.b - a.b) * t, 1.0f);
}
D2D1_COLOR_F withAlpha(D2D1_COLOR_F c, float a) { c.a = a; return c; }

struct Fmts {
    IDWriteTextFormat *left = nullptr, *right = nullptr, *bold = nullptr, *body = nullptr,
        *caption = nullptr, *mono = nullptr;
    void release() {
        for (auto** f : {&left, &right, &bold, &body, &caption, &mono})
            if (*f) { (*f)->Release(); *f = nullptr; }
    }
};

IDWriteTextFormat* makeFormat(App& app, float dip, DWRITE_FONT_WEIGHT weight,
                              DWRITE_TEXT_ALIGNMENT align, bool wrap,
                              const wchar_t* family = L"Segoe UI") {
    IDWriteTextFormat* f = nullptr;
    app.dwriteFactory->CreateTextFormat(family, nullptr, weight,
                                        DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL, dpi(app, dip),
                                        L"en-us", &f);
    if (!f) return nullptr;
    useUiFontFallback(app, f);
    f->SetTextAlignment(align);
    f->SetParagraphAlignment(wrap ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                  : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!wrap) {
        IDWriteInlineObject* sign = nullptr;
        if (SUCCEEDED(app.dwriteFactory->CreateEllipsisTrimmingSign(f, &sign)) && sign) {
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            f->SetTrimming(&trim, sign);
            sign->Release();
        }
    }
    return f;
}

void text(App& app, IDWriteTextFormat* f, const std::wstring& s, float x, float y,
          float w, float h, D2D1_COLOR_F c) {
    if (!f || s.empty() || w < 1.0f) return;
    app.brush->SetColor(c);
    app.renderTarget->DrawText(s.c_str(), (UINT32)s.size(), f,
                               D2D1::RectF(x, y, x + w, y + h), app.brush,
                               D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void fill(App& app, float l, float t, float r, float b, D2D1_COLOR_F c) {
    app.brush->SetColor(c);
    app.renderTarget->FillRectangle(D2D1::RectF(l, t, r, b), app.brush);
}

std::wstring formatWhen(unsigned long long ticks) {
    if (!ticks) return L"";
    FILETIME ft{(DWORD)(ticks & 0xFFFFFFFF), (DWORD)(ticks >> 32)}, local;
    SYSTEMTIME st, now;
    if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st)) return L"";
    GetLocalTime(&now);
    wchar_t buf[32];
    if (st.wYear == now.wYear)
        swprintf(buf, 32, L"%02d-%02d %02d:%02d", st.wMonth, st.wDay, st.wHour, st.wMinute);
    else
        swprintf(buf, 32, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return buf;
}

D2D1_COLOR_F entryColor(const D2DTheme& th, const Entry& e, bool selected) {
    if (selected) return th.heading;
    if (e.kind == Kind::Folder) return th.accent;
    if (e.kind == Kind::Other) return withAlpha(th.text, 0.38f);
    return th.text;
}

std::wstring entryLabel(const Entry& e) {
    return e.kind == Kind::Folder ? e.name + L"/" : e.name;
}

// A plain name list (parent column, folder preview): no meta, no hits
void drawNameList(App& app, const Fmts& fm, const dirmodel::Listing& l, float x,
                  float y, float w, float bottom, float rowH,
                  const std::wstring& highlight) {
    std::vector<int> idx = dirmodel::visible(l.entries, app.home.opts, L"");
    int rows = std::max(1, (int)((bottom - y) / rowH));
    int first = 0;
    for (int i = 0; i < (int)idx.size(); i++)
        if (l.entries[idx[i]].name == highlight) first = std::max(0, i - rows / 2);
    const D2DTheme& th = app.theme;
    for (int i = first; i < (int)idx.size() && y + rowH <= bottom; i++, y += rowH) {
        const Entry& e = l.entries[idx[i]];
        bool hl = !highlight.empty() && e.name == highlight;
        if (hl) fill(app, x, y, x + w, y + rowH, mix(th.background, th.accent, 0.22f));
        text(app, fm.left, entryLabel(e), x + dpi(app, 14), y, w - dpi(app, 24), rowH,
             entryColor(th, e, hl));
    }
}

// --- Home B folder tree ----------------------------------------------------

void treeLoadDir(HomeState& H, const std::wstring& dir) {
    if (H.treeCache.find(dir) != H.treeCache.end()) return;
    dirmodel::Listing listing;
    dirmodel::scan(dir, isDocName, listing);
    if (listing.ok) dirmodel::sortEntries(listing.entries, H.opts);
    H.treeCache.emplace(dir, std::move(listing));
}

void treeAppend(HomeState& H, const std::wstring& dir, int depth) {
    auto it = H.treeCache.find(dir);
    if (it == H.treeCache.end()) return;
    for (const Entry& e : it->second.entries) {
        if (e.kind == Kind::Other) continue;
        if (e.hidden && !H.opts.showHidden) continue;
        std::wstring path = dirmodel::join(dir, e.name);
        bool folder = e.kind == Kind::Folder;
        bool expanded = folder && H.expandedDirs.count(path) != 0;
        H.treeRows.push_back({path, e.name, depth, folder, expanded});
        if (expanded && depth < 16) treeAppend(H, path, depth + 1);
    }
}

void treeRebuild(HomeState& H) {
    H.treeRows.clear();
    if (!H.root.empty()) treeAppend(H, H.root, 0);
    int current = -1;
    for (int i = 0; i < (int)H.treeRows.size(); i++)
        if (H.treeRows[i].path == H.dir) { current = i; break; }
    if (H.treeSel < 0 || H.treeSel >= (int)H.treeRows.size()) H.treeSel = current;
}

void treeEnsurePath(HomeState& H, const std::wstring& dir) {
    if (H.root.empty() || !dirmodel::isInside(H.root, dir)) return;
    std::wstring rel = dir.substr(H.root.size());
    std::wstring cur = H.root;
    size_t i = 0;
    while (i < rel.size()) {
        while (i < rel.size() && (rel[i] == L'\\' || rel[i] == L'/')) i++;
        size_t j = i;
        while (j < rel.size() && rel[j] != L'\\' && rel[j] != L'/') j++;
        if (j > i) {
            cur = dirmodel::join(cur, rel.substr(i, j - i));
            H.expandedDirs.insert(cur);
            treeLoadDir(H, cur);
        }
        i = j;
    }
}

void treeSync(HomeState& H) {
    if (H.root.empty()) {
        H.treeRows.clear();
        H.treeSel = H.treeHover = -1;
        return;
    }
    if (H.treeRoot != H.root) {
        H.treeRoot = H.root;
        H.treeRows.clear();
        H.treeCache.clear();
        H.expandedDirs.clear();
        H.treeSel = H.treeHover = -1;
        treeLoadDir(H, H.root);
    }
    treeEnsurePath(H, H.dir);
    treeRebuild(H);
}

}  // namespace

// --- Public: state ---------------------------------------------------------

bool homeActive(const App& app) {
    return startPageActive(app) && app.home.booted;
}

// With a configured startup mode or at least one repository, Home owns the
// first window: the session's documents must not hide it behind a tab.
bool homeStartupWantsWindow(const Settings& settings) {
    return settings.startupMode == repos::kStartPicker || !settings.repoList.empty();
}

// First call decides what the launcher slot shows: the last repo, or the picker
void homeEnsureRoot(App& app) {
    HomeState& H = app.home;
    if (H.booted) return;
    H.booted = true;
    Settings st = loadSettings();
    repos::Startup d = repos::decideStartup(st.repoList, st.currentRepoId, st.startupMode, dirExists);
    if (d.openRepo) enterRepo(app, d.id);
    if (!d.openRepo || H.root.empty()) {
        std::wstring notice = H.repoNotice;
        homeShowPicker(H);
        H.refreshRows = false;
        refreshRows(app);
        if (!notice.empty()) H.repoNotice = notice;
    }
    dispatchScans(app, app.hwnd);
}

// --- Public: input ---------------------------------------------------------

bool homeOnKeyDown(App& app, HWND hwnd, WPARAM vk) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    HomeState& H = app.home;
    // Home B: Enter/l/→ opens a folder into the next column; h/←/Backspace
    // closes the rightmost column. Other keys stay with the shared logic.
    if (!ctrl && H.screen == HomeScreen::Home && !H.searching) {
        if (vk == VK_RETURN || vk == VK_RIGHT || vk == 'L') {
            if (openSelectedInColumn(app)) {
                settle(app, hwnd);
                return true;
            }
        }
        if (vk == VK_LEFT || vk == 'H' || vk == VK_BACK) {
            if (closeColumn(app)) {
                settle(app, hwnd);
                return true;
            }
        }
    }
    bool handled = homeKey(H, (unsigned)vk, ctrl, shift);
    if (handled) settle(app, hwnd);
    return handled;
}

bool homeOnChar(App& app, HWND hwnd, wchar_t c) {
    bool handled = homeChar(app.home, c);
    if (handled) settle(app, hwnd);
    return handled;
}

bool homeOnMouseMove(App& app, HWND, int x, int y) {
    x -= (int)documentViewportX(app);  // rects are recorded in the translated space
    int hoverColumn = -1, hoverRow = -1;
    for (const auto& hit : app.home.columnHits) {
        const HomeRect& r = hit.rect;
        if (x >= r.l && x < r.r && y >= r.t && y < r.b) {
            hoverColumn = hit.column;
            hoverRow = hit.row;
            break;
        }
    }
    if (hoverColumn == app.home.hoverColumn && hoverRow == app.home.hoverRow) return false;
    app.home.hoverColumn = hoverColumn;
    app.home.hoverRow = hoverRow;
    return true;
}

bool homeOnLButtonDown(App& app, HWND hwnd, int x, int y) {
    HomeState& H = app.home;
    x -= (int)documentViewportX(app);  // rects are recorded in the translated space
    auto in = [&](const HomeRect& r) { return x >= r.l && x < r.r && y >= r.t && y < r.b; };
    if (H.screen == HomeScreen::Picker) {
        if (H.hasBrowseBtn && in(H.browseBtn)) {
            H.pendingBrowse = true;
        } else {
            for (const auto& hit : H.rowHits) {
                if (in(hit.first)) { H.repoSel = hit.second; homeChooseRepo(H, hit.second); break; }
            }
        }
        settle(app, hwnd);
        return true;
    }

    if (in(H.searchHit)) {
        H.searching = true;
        H.swallow = 0;
        settle(app, hwnd);
        return true;
    }
    if (in(H.repoChipHit)) {
        H.screen = HomeScreen::Picker;
        H.refreshRows = true;
        settle(app, hwnd);
        return true;
    }

    for (const auto& hit : H.columnHits) {
        if (!in(hit.rect)) continue;
        const int ci = hit.column;
        const int row = hit.row;
        if (ci < (int)H.columns.size()) restoreColumn(H, ci);
        if (H.view.empty()) { settle(app, hwnd); return true; }
        if (row < 0 || row >= (int)H.view.size()) { settle(app, hwnd); return true; }
        const Entry& entry = H.current.entries[H.view[row]];
        H.sel = row;
        // A folder click grows the browser to the right. Files select first;
        // Enter or a double-click opens them in the viewer.
        if (entry.kind == Kind::Folder) {
            openSelectedInColumn(app);
            settle(app, hwnd);
            return true;
        }
        homeMoveTo(H, row);
        ULONGLONG now = GetTickCount64();
        bool dbl = ci == H.lastClickColumn && row == H.lastClickRow &&
                   now - H.lastClickTick <= GetDoubleClickTime();
        H.lastClickColumn = ci;
        H.lastClickRow = dbl ? -1 : row;
        H.lastClickTick = now;
        if (dbl) openSelectedInColumn(app);
        settle(app, hwnd);
        return true;
    }
    return true;  // the page is not a document: nothing to select underneath
}

void homeOnWheel(App& app, HWND hwnd, float notches) {
    HomeState& H = app.home;
    if (H.screen == HomeScreen::Picker) return;
    int maxTop = std::max(0, (int)H.view.size() - H.visibleRows);
    H.topRow = std::clamp(H.topRow - (int)(notches * 3.0f), 0, maxTop);
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool homeOnDropFolder(App& app, HWND hwnd, const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return false;
    addRepoFromFolder(app, path);
    settle(app, hwnd);
    return true;
}

void homeOnScanMessage(App& app, HWND hwnd, LPARAM lParam) {
    std::unique_ptr<ScanMsg> msg((ScanMsg*)lParam);
    if (!msg) return;
    if (msg->slot == kHomePreviewFile)
        homeApplyPreviewText(app.home, msg->gen, msg->path, std::move(msg->text));
    else
        homeApplyListing(app.home, msg->slot, msg->gen, std::move(msg->listing));
    settle(app, hwnd);
}

// --- Public: painting ------------------------------------------------------

// Page A (no repos yet): one button and a drop hint, centered.
// Page B (repos exist): the same block on the left, recent repos on the right.
static void renderPicker(App& app, const Fmts& fm) {
    HomeState& H = app.home;
    const D2DTheme& th = app.theme;
    const D2D1_COLOR_F dim = withAlpha(th.text, 0.55f), line = withAlpha(th.text, 0.12f);
    const D2D1_COLOR_F warn = D2D1::ColorF(0.86f, 0.46f, 0.42f);
    const float W = documentViewportWidth(app);
    const float top = chromeTopHeight(app), bottom = (float)app.height;
    const float pad = dpi(app, 28), ftrH = dpi(app, 30), bodyBottom = bottom - ftrH;
    const bool hasRows = !H.repoRows.empty();
    const bool split = hasRows && W >= dpi(app, 760);
    const float leftW = split ? std::min(W * 0.42f, dpi(app, 520)) : W;
    H.hasBrowseBtn = false;
    fill(app, 0, top, W, bottom, th.background);

    const DWRITE_TEXT_ALIGNMENT al =
        split ? DWRITE_TEXT_ALIGNMENT_LEADING : DWRITE_TEXT_ALIGNMENT_CENTER;
    IDWriteTextFormat* brand = makeFormat(app, 40, DWRITE_FONT_WEIGHT_SEMI_BOLD, al, false);
    IDWriteTextFormat* tag = makeFormat(app, 13, DWRITE_FONT_WEIGHT_NORMAL, al, false);
    IDWriteTextFormat* btn = makeFormat(app, 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                        DWRITE_TEXT_ALIGNMENT_CENTER, false);
    const float bx = split ? pad * 2 : pad, bw = split ? leftW - pad * 3 : W - 2 * pad;
    const float y0 = hasRows && !split
                         ? top + dpi(app, 24)
                         : top + std::max(dpi(app, 24), (bodyBottom - top - dpi(app, 190)) / 2);
    text(app, brand, L"tinta", bx, y0, bw, dpi(app, 56), th.accent);
    text(app, tag,
         hasRows ? L"Pick a repository, or open a new folder."
                 : L"Open a folder to start. It becomes your repository.",
         bx, y0 + dpi(app, 58), bw, dpi(app, 24), dim);
    const float btnW = std::min(dpi(app, 270), bw), btnH = dpi(app, 46);
    const float btnX = split ? bx : bx + (bw - btnW) / 2, btnY = y0 + dpi(app, 104);
    fill(app, btnX, btnY, btnX + btnW, btnY + btnH, th.accent);
    text(app, btn, L"Open folder…    Ctrl+O", btnX, btnY, btnW, btnH, th.background);
    H.browseBtn = HomeRect{btnX, btnY, btnX + btnW, btnY + btnH};
    H.hasBrowseBtn = true;
    text(app, tag, L"or drop a folder onto this window", bx, btnY + btnH + dpi(app, 14), bw,
         dpi(app, 22), withAlpha(th.text, 0.4f));
    for (auto* f : {brand, tag, btn}) if (f) f->Release();

    if (hasRows) {
        float lx, lw, ly;
        if (split) {
            lx = leftW + pad; lw = W - leftW - 2 * pad; ly = top + pad;
            fill(app, leftW, top, leftW + 1, bodyBottom, line);
        } else {
            lx = pad; lw = W - 2 * pad; ly = btnY + btnH + dpi(app, 56);
        }
        text(app, fm.caption, L"RECENT REPOSITORIES", lx, ly, lw, dpi(app, 22), dim);
        ly += dpi(app, 28);
        const float rowH = dpi(app, 56), whenW = dpi(app, 120);
        for (int i = 0; i < (int)H.repoRows.size() && ly + rowH <= bodyBottom; i++, ly += rowH) {
            const RepoRow& r = H.repoRows[i];
            bool sel = i == H.repoSel, hover = i == H.hoverRow;
            if (sel) fill(app, lx, ly, lx + lw, ly + rowH, mix(th.background, th.accent, 0.22f));
            else if (hover) fill(app, lx, ly, lx + lw, ly + rowH, mix(th.background, th.text, 0.06f));
            if (i < 9)
                text(app, fm.left, std::to_wstring(i + 1), lx + dpi(app, 12), ly, dpi(app, 20),
                     rowH, th.accent);
            D2D1_COLOR_F nameC = r.missing ? warn : (sel ? th.heading : th.text);
            text(app, fm.left, r.name, lx + dpi(app, 40), ly + dpi(app, 6),
                 lw - dpi(app, 52) - whenW, rowH / 2, nameC);
            text(app, fm.caption, r.path, lx + dpi(app, 40), ly + rowH / 2, lw - dpi(app, 52),
                 rowH / 2 - dpi(app, 4), dim);
            text(app, fm.right, r.missing ? std::wstring(L"folder missing") : formatWhen(r.lastOpened),
                 lx + lw - dpi(app, 12) - whenW, ly + dpi(app, 6), whenW, rowH / 2,
                 r.missing ? warn : dim);
            H.rowHits.push_back({HomeRect{lx, ly, lx + lw, ly + rowH}, i});
        }
    }

    fill(app, 0, bodyBottom, W, bottom, mix(th.background, th.text, th.isDark ? 0.05f : -0.04f));
    std::wstring hint = hasRows ? L"1-9 open    ↑ ↓ select    Enter open    o add folder    x forget"
                                : L"Ctrl+O choose a folder    or drop one here";
    if (hasRows && !H.root.empty()) hint += L"    Esc back";
    if (!H.repoNotice.empty()) text(app, fm.left, H.repoNotice, pad, bodyBottom, W - 2 * pad, ftrH, warn);
    else text(app, fm.left, hint, pad, bodyBottom, W - 2 * pad, ftrH, dim);
}

void renderHome(App& app) {
    HomeState& H = app.home;
    H.rowHits.clear();
    H.columnHits.clear();
    if (!app.renderTarget || !app.brush || !app.dwriteFactory) return;
    homeEnsureRoot(app);

    if (H.screen == HomeScreen::Picker) {
        Fmts fm;
        fm.left = makeFormat(app, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false);
        fm.right = makeFormat(app, 12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, false);
        fm.bold = makeFormat(app, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false);
        fm.body = makeFormat(app, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        fm.caption = makeFormat(app, 11.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false);
        fm.mono = makeFormat(app, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false, L"Cascadia Mono");
        renderPicker(app, fm);
        fm.release();
        return;
    }
    if (H.root.empty()) return;

    // A new repository starts from a single centered column.
    if (H.columnsRoot != H.root) {
        H.columns.clear();
        H.columnsRoot = H.root;
        H.hoverColumn = -1;
        H.lastClickColumn = -1;
    }

    const D2DTheme& th = app.theme;
    const D2D1_COLOR_F dim = withAlpha(th.text, 0.55f);
    const D2D1_COLOR_F line = withAlpha(th.text, 0.12f);
    const float W = documentViewportWidth(app);
    const float top = chromeTopHeight(app), bottom = (float)app.height;
    const float hdrH = dpi(app, 44), ftrH = dpi(app, 30);
    const float bodyTop = top + hdrH, bodyBottom = bottom - ftrH;
    const float pad = dpi(app, 14);
    const float rowH = dpi(app, 30);

    Fmts fm;
    fm.left = makeFormat(app, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fm.right = makeFormat(app, 12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, false);
    fm.bold = makeFormat(app, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fm.body = makeFormat(app, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    fm.caption = makeFormat(app, 11.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fm.mono = makeFormat(app, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false, L"Cascadia Mono");

    fill(app, 0, top, W, bottom, th.background);

    // Top bar: repository chip, breadcrumb, search box
    fill(app, 0, top, W, bodyTop, mix(th.background, th.text, th.isDark ? 0.04f : -0.04f));
    fill(app, 0, bodyTop - 1, W, bodyTop, line);

    std::wstring repoName = dirmodel::leafOf(H.root);
    if (repoName.empty()) repoName = H.root;
    std::wstring repoChip = repoName + L"  \x25BE";
    float chipW = measureText(app, repoChip, fm.bold) + dpi(app, 22);
    float chipX = pad, chipY = top + dpi(app, 8), chipH = hdrH - dpi(app, 16);
    H.repoChipHit = HomeRect{chipX, chipY, chipX + chipW, chipY + chipH};
    D2D1_COLOR_F chipBorder = th.accent; chipBorder.a = 0.55f;
    app.brush->SetColor(chipBorder);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(chipX, chipY, chipX + chipW, chipY + chipH),
                          dpi(app, 4), dpi(app, 4)), app.brush, 1.0f);
    text(app, fm.bold, repoChip, chipX + dpi(app, 11), chipY, chipW - dpi(app, 16), chipH,
         th.accent);

    const float searchW = std::min(dpi(app, 290), W * 0.34f);
    const float searchX = W - pad - searchW;
    std::wstring crumb = repoName;
    if (H.dir.size() > H.root.size()) {
        std::wstring rel = H.dir.substr(H.root.size());
        while (!rel.empty() && (rel[0] == L'\\' || rel[0] == L'/')) rel.erase(0, 1);
        for (auto& c : rel) if (c == L'\\') c = L'/';
        crumb += L" / " + rel;
    }
    float crumbX = chipX + chipW + dpi(app, 16);
    text(app, fm.mono, crumb, crumbX, top, std::max(0.0f, searchX - crumbX - pad), hdrH, dim);

    D2D1_RECT_F searchRect = D2D1::RectF(searchX, chipY, searchX + searchW, chipY + chipH);
    H.searchHit = HomeRect{searchRect.left, searchRect.top, searchRect.right, searchRect.bottom};
    D2D1_COLOR_F searchBorder = th.text; searchBorder.a = 0.18f;
    app.brush->SetColor(searchBorder);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(searchRect, dpi(app, 4), dpi(app, 4)), app.brush, 1.0f);
    if (H.searching) {
        text(app, fm.mono, L"/ " + H.query + L"\x2502", searchRect.left + dpi(app, 10),
             searchRect.top, searchW - dpi(app, 20), chipH, th.accent);
    } else {
        text(app, fm.mono, tr(app, "home.search"), searchRect.left + dpi(app, 10),
             searchRect.top, searchW - dpi(app, 20), chipH, dim);
    }

    // Miller columns: the active column is on the right; clicking a folder
    // pushes its contents as a new column (maximum three in total).
    struct ColRef {
        const std::wstring* path;
        const dirmodel::Listing* listing;
        const std::vector<int>* view;
        int sel;
        int topRow;
        bool active;
    };
    std::vector<ColRef> cols;
    cols.reserve(3);
    for (auto& c : H.columns)
        cols.push_back({&c.path, &c.listing, &c.view, c.sel, c.topRow, false});
    cols.push_back({&H.dir, &H.current, &H.view, H.sel, H.topRow, true});
    const int count = (int)cols.size();
    const float gap = dpi(app, 10);
    float colW;
    if (count == 1) colW = std::min(dpi(app, 640), W - 2 * pad);
    else if (count == 2) colW = std::min(dpi(app, 460), (W - 2 * pad - gap) / 2);
    else colW = std::min(dpi(app, 400), (W - 2 * pad - 2 * gap) / 3);
    colW = std::max(colW, dpi(app, 170));
    const float totalW = count * colW + (count - 1) * gap;
    const float startX = (W - totalW) / 2;
    const float headerH = dpi(app, 30);
    const float rowsTop = bodyTop + headerH + dpi(app, 4);
    const int visibleRows = std::max(1, (int)((bodyBottom - rowsTop) / rowH));
    H.visibleRows = visibleRows;

    for (int ci = 0; ci < count; ci++) {
        const ColRef& col = cols[ci];
        const float x = startX + ci * (colW + gap);
        const bool active = col.active;
        const D2D1_COLOR_F surface = mix(th.background, th.text, th.isDark ? (active ? 0.035f : 0.018f)
                                                                         : (active ? -0.025f : -0.012f));
        fill(app, x, bodyTop, x + colW, bodyBottom, surface);
        D2D1_COLOR_F border = active ? withAlpha(th.accent, 0.30f) : line;
        app.brush->SetColor(border);
        app.renderTarget->DrawRectangle(
            D2D1::RectF(x + 0.5f, bodyTop + 0.5f, x + colW - 0.5f, bodyBottom - 0.5f),
            app.brush, 1.0f);
        fill(app, x, bodyTop + headerH, x + colW, bodyTop + headerH + 1, line);

        std::wstring colName = (*col.path == H.root) ? repoName : dirmodel::leafOf(*col.path);
        if (colName.empty()) colName = *col.path;
        text(app, fm.caption, colName, x + pad, bodyTop, colW - 2 * pad, headerH,
             active ? th.accent : dim);

        int topRow = col.topRow;
        int maxTop = std::max(0, (int)col.view->size() - visibleRows);
        topRow = std::clamp(topRow, 0, maxTop);
        if (active && col.sel >= 0) {
            if (col.sel < topRow) topRow = col.sel;
            else if (col.sel >= topRow + visibleRows) topRow = col.sel - visibleRows + 1;
            topRow = std::clamp(topRow, 0, maxTop);
            H.topRow = topRow;
        }

        const float sizeW = dpi(app, 64), whenW = dpi(app, 92);
        const bool showMeta = colW > dpi(app, 360);
        const float nameW = colW - pad * 2 - (showMeta ? sizeW + whenW : 0.0f);
        float y = rowsTop;
        if (col.view->empty()) {
            const wchar_t* msg = !col.listing->ok ? L"Can't read this folder" : L"Empty folder";
            text(app, fm.mono, msg, x + pad, y, colW - 2 * pad, rowH, dim);
        }
        for (int n = 0; n < visibleRows; n++) {
            int row = topRow + n;
            if (row >= (int)col.view->size()) break;
            const Entry& e = col.listing->entries[(*col.view)[row]];
            bool selected = row == col.sel;
            bool hovered = ci == H.hoverColumn && row == H.hoverRow;
            if (selected) fill(app, x + 1, y, x + colW - 1, y + rowH, mix(th.background, th.accent, 0.24f));
            else if (hovered) fill(app, x + 1, y, x + colW - 1, y + rowH, mix(th.background, th.text, 0.06f));
            text(app, fm.mono, entryLabel(e), x + pad, y, nameW, rowH,
                 entryColor(th, e, selected));
            if (showMeta && e.kind != Kind::Folder)
                text(app, fm.right, dirmodel::formatSize(e.size), x + pad + nameW, y,
                     sizeW, rowH, dim);
            if (showMeta)
                text(app, fm.right, formatWhen(e.mtime), x + pad + nameW + sizeW, y,
                     whenW, rowH, dim);
            H.columnHits.push_back({HomeRect{x, y, x + colW, y + rowH}, ci, row});
            y += rowH;
        }
    }

    // Footer: active-column position and key hints
    fill(app, 0, bodyBottom, W, bottom, mix(th.background, th.text, th.isDark ? 0.05f : -0.04f));
    fill(app, 0, bodyBottom, W, bodyBottom + 1, line);
    if (!H.repoNotice.empty()) {
        text(app, fm.caption, H.repoNotice, pad, bodyBottom, W - 2 * pad, ftrH,
             D2D1::ColorF(0.86f, 0.46f, 0.42f));
    } else {
        std::wstring countText = H.view.empty()
            ? std::wstring(L"0 / 0")
            : std::to_wstring(H.sel + 1) + L" / " + std::to_wstring(H.view.size());
        text(app, fm.caption, countText, pad, bodyBottom, dpi(app, 90), ftrH, dim);
        text(app, fm.caption, tr(app, "home.footer"), pad + dpi(app, 90), bodyBottom,
             W - pad * 2 - dpi(app, 90), ftrH, dim);
    }
    fm.release();
}
