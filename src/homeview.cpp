// Home view shell: threads, input routing and Direct2D painting.
// The navigation rules live in home_logic.cpp and are unit-tested there.

#include "homeview.h"

#include <dwrite_1.h>
#include <imm.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cwctype>
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

// Open a file from Home into a new tab. Home is the fixed workspace tab and
// stays at index 0; only document windows without Home keep the old hand-over.
void openFromHome(App& app, HWND hwnd, const std::wstring& path) {
    const bool wasHome = !app.home.root.empty();
    if (wasHome) tabsEnsureHome(app);
    tabsInit(app);
    int shellId = app.tabs[app.activeTab].id;
    tabOpenPath(app, hwnd, toUtf8(path));
    if (!wasHome) {
        for (int i = 0; i < (int)app.tabs.size(); i++) {
            if (app.tabs[i].id == shellId) { tabCloseIndex(app, hwnd, i); break; }
        }
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
        rows.push_back({r.id, toWide(r.name), toWide(r.path), r.lastOpened,
                        !dirExists(r.path), r.id == st.currentRepoId});
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
    tabsEnsureHome(app);
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

// Point an existing repository at a new folder without losing its id, name or
// place in settings. Used when the original folder was moved or renamed.
void relocateRepo(App& app, HWND hwnd, const std::string& id) {
    Settings st = loadSettings();
    int i = repos::indexOfId(st.repoList, id);
    if (i < 0) return;
    std::wstring folder = pickFolder(hwnd);
    if (folder.empty()) return;  // dialog cancelled: keep the current state
    const std::string path = toUtf8(folder);
    int duplicate = repos::indexOfPath(st.repoList, path);
    if (duplicate >= 0 && st.repoList[duplicate].id != id) {
        app.home.repoNotice = L"That folder is already a repository: " +
                              toWide(st.repoList[duplicate].name);
        return;
    }

    repos::Repo& repo = st.repoList[i];
    repo.path = path;
    if (repo.name.empty()) repo.name = toUtf8(dirmodel::leafOf(folder));
    repo.lastDir.clear();
    repos::touch(st.repoList, id, nowTicks());
    st.currentRepoId = id;
    const bool saved = saveSettings(st);
    refreshRows(app);
    if (activateRepo(app, st, id) && !saved) {
        app.home.repoNotice =
            L"Repository relocated for this session, but settings could not be saved: " +
            getSettingsPath();
    }
}

// After any state change: run the side effects the logic left behind
// (dialogs, settings, scans, opening a file), then repaint
void pasteClipboard(App& app, HWND hwnd, bool overwrite);
void preparePaste(App& app, HWND hwnd);
void undoLastTransfer(App& app, HWND hwnd);

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
    if (!H.pendingRelocate.empty()) {
        std::string id = std::move(H.pendingRelocate);
        H.pendingRelocate.clear();
        relocateRepo(app, hwnd, id);
    }
    if (!H.pendingDefault.empty()) {
        std::string id = std::move(H.pendingDefault);
        H.pendingDefault.clear();
        Settings st = loadSettings();
        int i = repos::indexOfId(st.repoList, id);
        if (i < 0 || !dirExists(st.repoList[i].path)) {
            H.repoNotice = L"That repository cannot be the startup repository.";
        } else {
            st.currentRepoId = id;
            st.startupMode = repos::kStartLast;
            if (!saveSettings(st))
                H.repoNotice =
                    L"Startup repository changed for this session, but settings could not be saved: " +
                    getSettingsPath();
            refreshRows(app);
        }
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
    if (H.pendingPaste) {
        H.pendingPaste = false;
        preparePaste(app, hwnd);
    }
    if (H.pendingUndo) {
        H.pendingUndo = false;
        undoLastTransfer(app, hwnd);
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

// --- Home file operations --------------------------------------------------

bool pathExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

DWORD recyclePath(const std::wstring& path) {
    std::wstring from = path;
    from.push_back(L'\0');  // SHFileOperation needs a double-NUL list
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    int result = SHFileOperationW(&op);
    if (result != 0) return (DWORD)result;
    return op.fAnyOperationsAborted ? ERROR_CANCELLED : 0;
}

DWORD shellTransfer(const std::wstring& source, const std::wstring& target,
                    bool move) {
    std::wstring from = source;
    from.push_back(L'\0');
    std::wstring to = target;
    to.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = move ? FO_MOVE : FO_COPY;
    op.pFrom = from.c_str();
    op.pTo = to.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI |
                FOF_NOCONFIRMMKDIR;
    int result = SHFileOperationW(&op);
    if (result != 0) return (DWORD)result;
    return op.fAnyOperationsAborted ? ERROR_CANCELLED : 0;
}

bool isDirectoryPath(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// Ctrl+V: copy/move the internal clipboard into the current directory.
// Folder-into-itself is always skipped. Existing names are skipped unless the
// user explicitly chose Overwrite (files only; folder merging is not offered).
void pasteClipboard(App& app, HWND hwnd, bool overwrite) {
    HomeState& H = app.home;
    if (H.clipboard.empty() || H.dir.empty()) return;
    const std::wstring dir = H.dir;
    std::vector<std::wstring> remaining;
    std::vector<HomeTransfer> completed;
    bool overwrote = false;
    unsigned pasted = 0, skipped = 0, failed = 0;
    for (const auto& source : H.clipboard) {
        std::wstring target = dirmodel::join(dir, dirmodel::leafOf(source));
        const bool same = _wcsicmp(source.c_str(), target.c_str()) == 0;
        if (!pathExists(source) || same || dirmodel::isInside(source, dir)) {
            skipped++;
            if (H.clipboardCut) remaining.push_back(source);
            continue;
        }
        if (pathExists(target)) {
            if (!overwrite || isDirectoryPath(source) ||
                isDirectoryPath(target)) {
                skipped++;
                if (H.clipboardCut) remaining.push_back(source);
                continue;
            }
            overwrote = true;  // replacing an existing file is not undoable
            bool ok = false;
            if (H.clipboardCut) {
                ok = MoveFileExW(source.c_str(), target.c_str(),
                                 MOVEFILE_REPLACE_EXISTING |
                                     MOVEFILE_COPY_ALLOWED) != 0;
            } else {
                ok = CopyFileW(source.c_str(), target.c_str(), FALSE) != 0;
            }
            if (!ok) {
                failed++;
                remaining.push_back(source);
            } else {
                pasted++;
                completed.push_back({source, target});
            }
            continue;
        }
        if (shellTransfer(source, target, H.clipboardCut)) {
            failed++;
            remaining.push_back(source);
        } else {
            pasted++;
            completed.push_back({source, target});
        }
    }
    if (pasted && !overwrote) {
        H.undoTransfers = std::move(completed);
        H.undoIsMove = H.clipboardCut;
    } else if (overwrote) {
        H.undoTransfers.clear();
    }
    if (H.clipboardCut) H.clipboard = std::move(remaining);
    if (pasted) {
        homeNavigate(H, dir, L"");
    } else {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    wchar_t message[256];
    swprintf_s(message, _countof(message), tr(app, "home.paste_result"),
               pasted, skipped, failed);
    H.fileNotice = message;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Ctrl+Z: put the last moved items back, or recycle the last copied items.
// Overwriting pastes are deliberately not undoable (the old bytes are gone).
void undoLastTransfer(App& app, HWND hwnd) {
    HomeState& H = app.home;
    if (H.undoTransfers.empty()) {
        H.fileNotice = tr(app, "home.undo_none");
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    const bool move = H.undoIsMove;
    unsigned ok = 0, failed = 0;
    for (const HomeTransfer& item : H.undoTransfers) {
        if (move) {
            if (!pathExists(item.target) || pathExists(item.source)) {
                failed++;
                continue;
            }
            if (MoveFileExW(item.target.c_str(), item.source.c_str(),
                            MOVEFILE_COPY_ALLOWED)) {
                ok++;
            } else {
                failed++;
            }
        } else {
            if (!pathExists(item.target)) {
                failed++;
                continue;
            }
            if (recyclePath(item.target)) failed++;
            else ok++;
        }
    }
    H.undoTransfers.clear();
    homeNavigate(H, H.dir, L"");
    wchar_t message[256];
    swprintf_s(message, _countof(message),
               tr(app, move ? "home.undo_move" : "home.undo_copy"), ok, failed);
    H.fileNotice = message;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// A plain Ctrl+V with existing names asks first. With no conflicts it is the
// old direct paste.
void preparePaste(App& app, HWND hwnd) {
    HomeState& H = app.home;
    H.pasteConflicts.clear();
    if (H.clipboard.empty() || H.dir.empty()) return;
    for (const auto& source : H.clipboard) {
        if (!pathExists(source)) continue;
        std::wstring target =
            dirmodel::join(H.dir, dirmodel::leafOf(source));
        // Folder merging is not offered, so only file/file collisions ask.
        if (pathExists(target) && !isDirectoryPath(source) &&
            !isDirectoryPath(target))
            H.pasteConflicts.push_back(dirmodel::leafOf(source));
    }
    if (H.pasteConflicts.empty()) {
        pasteClipboard(app, hwnd, false);
        return;
    }
    H.dialog = HomeDialogKind::ConfirmPasteConflicts;
    H.pasteConflictChoice = 0;
    H.dialogError = HomeDialogError::None;
    H.dialogErrorCode = 0;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Runs the modal's pending action. On success the dialog closes and the
// active directory rescans; on failure the dialog stays open with an error.
void homeCommitDialog(App& app, HWND hwnd) {
    HomeState& H = app.home;
    H.dialogCommit = false;
    if (H.dialog == HomeDialogKind::None) return;
    const std::wstring dir = H.dir;
    auto fail = [&](HomeDialogError error, DWORD code = 0) {
        H.dialogError = error;
        H.dialogErrorCode = code;
        InvalidateRect(hwnd, nullptr, FALSE);
    };

    if (H.dialog == HomeDialogKind::ConfirmPasteConflicts) {
        const int choice = H.pasteConflictChoice;
        homeDialogClose(H);
        pasteClipboard(app, hwnd, choice == 1);
        return;
    }
    if (H.dialog == HomeDialogKind::ConfirmDelete) {
        if (H.dialogTargets.size() > 1) {
            unsigned deleted = 0, failed = 0;
            for (const std::wstring& name : H.dialogTargets) {
                DWORD code = recyclePath(dirmodel::join(dir, name));
                if (code) failed++;
                else deleted++;
            }
            homeDialogClose(H);
            homeNavigate(H, dir, L"");
            wchar_t message[256];
            if (failed) {
                swprintf_s(message, _countof(message),
                           tr(app, "home.delete_partial"), deleted, failed);
                H.repoNotice = message;
            } else {
                swprintf_s(message, _countof(message),
                           tr(app, "home.delete_result"), deleted);
                H.fileNotice = message;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        const std::wstring target =
            H.dialogTargets.empty() ? H.dialogOriginal : H.dialogTargets.front();
        DWORD code = recyclePath(dirmodel::join(dir, target));
        if (code) {
            fail(HomeDialogError::Failed, code);
            return;
        }
        homeDialogClose(H);
        homeNavigate(H, dir, L"");
        return;
    }

    const std::wstring name = H.dialogText;
    if (name.empty()) { fail(HomeDialogError::Empty); return; }
    const std::wstring target = dirmodel::join(dir, name);
    if (H.dialog == HomeDialogKind::NewFile) {
        HANDLE file = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            DWORD code = GetLastError();
            fail(pathExists(target) ? HomeDialogError::Exists
                                    : HomeDialogError::Failed, code);
            return;
        }
        CloseHandle(file);
    } else if (H.dialog == HomeDialogKind::NewFolder) {
        if (!CreateDirectoryW(target.c_str(), nullptr)) {
            DWORD code = GetLastError();
            fail(pathExists(target) ? HomeDialogError::Exists
                                    : HomeDialogError::Failed, code);
            return;
        }
    } else if (H.dialog == HomeDialogKind::Rename) {
        const std::wstring source = dirmodel::join(dir, H.dialogOriginal);
        if (source == target) {
            homeDialogClose(H);
            return;
        }
        if (pathExists(target)) { fail(HomeDialogError::Exists); return; }
        if (!MoveFileW(source.c_str(), target.c_str())) {
            fail(HomeDialogError::Failed, GetLastError());
            return;
        }
    }
    homeDialogClose(H);
    homeNavigate(H, dir, name);
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
        *caption = nullptr, *mono = nullptr, *tag = nullptr, *button = nullptr;
    void release() {
        for (auto** f : {&left, &right, &bold, &body, &caption, &mono, &tag, &button})
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

void fillRound(App& app, float l, float t, float r, float b, float radius, D2D1_COLOR_F c) {
    if (r <= l || b <= t) return;
    const float rad = std::max(0.0f, std::min(radius, std::min(r - l, b - t) / 2.0f));
    app.brush->SetColor(c);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(l, t, r, b), rad, rad), app.brush);
}

void strokeRound(App& app, float l, float t, float r, float b, float radius,
                 D2D1_COLOR_F c, float width) {
    const float rad = std::max(0.0f, std::min(radius, std::min(r - l, b - t) / 2.0f));
    app.brush->SetColor(c);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(l, t, r, b), rad, rad), app.brush, width);
}

// In-place name editor used by rename and by the new-file/new-folder phantom
// row. It owns the IME anchor so composition follows the inline caret.
void inlineNameField(App& app, HomeState& H, const Fmts& fm, float x, float y,
                     float w, float h, const D2DTheme& th) {
    H.dialogFieldHit = HomeRect{x, y, x + w, y + h};
    fillRound(app, x, y, x + w, y + h, dpi(app, 5),
              mix(th.background, th.text, th.isDark ? 0.07f : -0.04f));
    strokeRound(app, x + 0.5f, y + 0.5f, x + w - 0.5f, y + h - 0.5f,
                dpi(app, 5), withAlpha(th.accent, 0.62f), 1.0f);
    const float textX = x + dpi(app, 8);
    const int caret = std::clamp(H.dialogCaret, 0, (int)H.dialogText.size());
    const std::wstring before = H.dialogText.substr(0, caret);
    const float caretX = textX + measureText(app, before, fm.mono);
    H.dialogCaretX = caretX;
    H.dialogCaretY = y + h / 2.0f;
    if (H.dialogText.empty()) {
        const wchar_t* placeholder =
            H.dialog == HomeDialogKind::NewFolder ? tr(app, "home.new_folder")
                                                  : tr(app, "home.new_file");
        text(app, fm.mono, placeholder, textX, y, w - dpi(app, 16), h,
             withAlpha(th.text, 0.35f));
    } else {
        text(app, fm.mono, H.dialogText, textX, y, w - dpi(app, 16), h, th.text);
    }
    fill(app, caretX, y + dpi(app, 5), caretX + dpi(app, 1.6f),
         y + h - dpi(app, 5), th.accent);
}

// A small badge label for a file row: MD, TXT, PDF, IMG, ...
std::wstring fileTypeTag(const Entry& e) {
    if (e.kind == Kind::Folder) return L"";
    const size_t dot = e.name.find_last_of(L'.');
    std::wstring ext = dot == std::wstring::npos ? std::wstring() : e.name.substr(dot + 1);
    for (auto& c : ext) c = (wchar_t)std::towupper(c);
    if (ext == L"MD" || ext == L"MDX" || ext == L"MARKDOWN") return L"MD";
    if (ext == L"PNG" || ext == L"JPG" || ext == L"JPEG" || ext == L"GIF" ||
        ext == L"WEBP" || ext == L"BMP" || ext == L"SVG" || ext == L"AVIF")
        return L"IMG";
    if (ext == L"HTML" || ext == L"HTM") return L"HTML";
    if (ext == L"YML" || ext == L"YAML") return L"YAML";
    if (ext.empty() || ext.size() > 4) return L"FILE";
    return ext;
}

void drawSearchIcon(App& app, float x, float y, float h, D2D1_COLOR_F c) {
    const float radius = dpi(app, 4.2f);
    const float cx = x + radius;
    const float cy = y + h / 2.0f;
    app.brush->SetColor(c);
    app.renderTarget->DrawEllipse(
        D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), app.brush, dpi(app, 1.25f));
    app.renderTarget->DrawLine(
        D2D1::Point2F(cx + radius * 0.72f, cy + radius * 0.72f),
        D2D1::Point2F(cx + radius * 1.55f, cy + radius * 1.55f), app.brush, dpi(app, 1.35f));
}

// Thin scroll position rail at the right edge of an overflowing column.
void drawColumnScroll(App& app, float right, float top, float height, int total, int visible,
                      int first, D2D1_COLOR_F track, D2D1_COLOR_F thumb) {
    if (total <= visible || height < dpi(app, 18)) return;
    const float width = dpi(app, 3);
    const float x = right - dpi(app, 10);
    fillRound(app, x, top, x + width, top + height, width / 2.0f, track);
    float thumbH = std::max(dpi(app, 24), height * (float)visible / (float)total);
    thumbH = std::min(thumbH, height);
    const int maxFirst = std::max(1, total - visible);
    const float frac = std::clamp((float)first / (float)maxFirst, 0.0f, 1.0f);
    const float y = top + (height - thumbH) * frac;
    fillRound(app, x, y, x + width, y + thumbH, width / 2.0f, thumb);
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
    if (!startPageActive(app) || !app.home.booted) return false;
    // Workspace windows have a fixed Home tab: only that tab shows Home.
    // Windows without a Home tab (single-document launches, satellites)
    // keep the old "any empty viewer tab is the start page" behaviour.
    bool hasHome = false;
    for (const App::DocTab& tab : app.tabs) hasHome = hasHome || tab.home;
    if (!hasHome) return true;
    return app.activeTab >= 0 && app.activeTab < (int)app.tabs.size() &&
           app.tabs[app.activeTab].home;
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

void homeRefresh(App& app, HWND hwnd) { settle(app, hwnd); }

bool homePositionImeWindow(App& app, HWND hwnd) {
    HomeState& H = app.home;
    if (H.dialog == HomeDialogKind::None ||
        H.dialog == HomeDialogKind::ConfirmDelete ||
        H.dialogFieldHit.r <= H.dialogFieldHit.l) {
        return false;
    }
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) return false;
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = {(LONG)H.dialogCaretX, (LONG)H.dialogCaretY};
    ImmSetCompositionWindow(himc, &composition);
    CANDIDATEFORM candidate{};
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = composition.ptCurrentPos;
    ImmSetCandidateWindow(himc, &candidate);
    ImmReleaseContext(hwnd, himc);
    return true;
}

bool homeImeComposition(App& app, HWND hwnd, LPARAM lParam) {
    HomeState& H = app.home;
    if (H.dialog == HomeDialogKind::None ||
        H.dialog == HomeDialogKind::ConfirmDelete) {
        return false;
    }
    if (!(lParam & GCS_RESULTSTR)) return false;
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) return false;
    LONG bytes = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
    if (bytes > 0) {
        std::wstring text((size_t)bytes / sizeof(wchar_t), L'\0');
        LONG got = ImmGetCompositionStringW(himc, GCS_RESULTSTR, text.data(), bytes);
        if (got > 0) {
            for (wchar_t c : text) {
                if (c >= 32) homeChar(H, c);
            }
            // The IME result replaces the WM_CHAR stream for this commit.
            app.swallowCharsUntil = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(200);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
    ImmReleaseContext(hwnd, himc);
    return true;
}

bool homeOnKeyDown(App& app, HWND hwnd, WPARAM vk) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    HomeState& H = app.home;
    // The file-operation modal owns the keyboard while it is open.
    if (H.dialog != HomeDialogKind::None) {
        homeKey(H, (unsigned)vk, ctrl, shift);
        if (H.dialogCommit) homeCommitDialog(app, hwnd);
        settle(app, hwnd);
        return true;
    }
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
    // The key that opened the modal must not leak its typed character into
    // the new name field (same guard as the '/' search opener).
    if (handled && H.dialog != HomeDialogKind::None) {
        app.swallowCharsUntil = std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(180);
    }
    if (handled) settle(app, hwnd);
    return handled;
}

bool homeOnChar(App& app, HWND hwnd, wchar_t c) {
    bool handled = homeChar(app.home, c);
    if (handled) settle(app, hwnd);
    return handled;
}

bool homeOnMouseMove(App& app, HWND hwnd, int x, int y) {
    if (app.home.dialog == HomeDialogKind::ConfirmPasteConflicts) {
        x -= (int)documentViewportX(app);
        HomeState& H = app.home;
        auto in = [&](const HomeRect& r) {
            return x >= r.l && x < r.r && y >= r.t && y < r.b;
        };
        int choice = H.pasteConflictChoice;
        if (in(H.dialogPrimaryHit)) choice = 1;
        else if (in(H.dialogCancelHit)) choice = 0;
        if (choice != H.pasteConflictChoice) {
            H.pasteConflictChoice = choice;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return true;
    }
    if (app.home.dialog != HomeDialogKind::None) return false;
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
        H.confirmRemove.clear();
        if (H.hasBrowseBtn && in(H.browseBtn)) {
            H.pendingBrowse = true;
        } else {
            for (const auto& hit : H.rowHits) {
                if (!in(hit.first)) continue;
                H.repoSel = hit.second;
                const unsigned long long now = nowTicks();
                const bool doubleClick =
                    H.lastClickRow == hit.second &&
                    now - H.lastClickTick < 5000000ULL;  // 500 ms in FILETIME ticks
                H.lastClickRow = hit.second;
                H.lastClickTick = now;
                if (doubleClick && hit.second < (int)H.repoRows.size() &&
                    H.repoRows[hit.second].missing) {
                    H.pendingRelocate = H.repoRows[hit.second].id;
                    H.repoNotice.clear();
                } else {
                    homeChooseRepo(H, hit.second);
                }
                break;
            }
        }
        settle(app, hwnd);
        return true;
    }

    if (H.dialog == HomeDialogKind::ConfirmPasteConflicts) {
        if (in(H.dialogPrimaryHit)) {
            H.pasteConflictChoice = 1;  // Overwrite
            H.dialogCommit = true;
            homeCommitDialog(app, hwnd);
        } else if (in(H.dialogCancelHit)) {
            H.pasteConflictChoice = 0;  // Skip
            H.dialogCommit = true;
            homeCommitDialog(app, hwnd);
        }
        settle(app, hwnd);
        return true;
    }
    if (H.dialog != HomeDialogKind::None) {
        if (in(H.dialogPrimaryHit)) {
            H.dialogCommit = true;
            homeCommitDialog(app, hwnd);
        } else if (in(H.dialogCancelHit)) {
            homeDialogClose(H);
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
    if (H.screen == HomeScreen::Picker || H.dialog != HomeDialogKind::None) return;
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
            std::wstring state;
            D2D1_COLOR_F stateC = dim;
            if (r.missing) {
                state = sel ? L"missing \u00B7 r" : L"folder missing";
                stateC = warn;
            } else if (r.current) {
                state = L"default";
                stateC = th.accent;
            } else {
                state = formatWhen(r.lastOpened);
            }
            text(app, fm.right, state,
                 lx + lw - dpi(app, 12) - whenW, ly + dpi(app, 6), whenW, rowH / 2,
                 stateC);
            H.rowHits.push_back({HomeRect{lx, ly, lx + lw, ly + rowH}, i});
        }
    }

    fill(app, 0, bodyBottom, W, bottom, mix(th.background, th.text, th.isDark ? 0.05f : -0.04f));
    std::wstring hint = hasRows
        ? L"1-9 open   ↑↓ select   Enter open   o add   r relocate   d default   x forget"
        : L"Ctrl+O choose a folder    or drop one here";
    if (hasRows && !H.root.empty()) hint += L"    Esc back";
    if (!H.repoNotice.empty()) text(app, fm.left, H.repoNotice, pad, bodyBottom, W - 2 * pad, ftrH, warn);
    else text(app, fm.left, hint, pad, bodyBottom, W - 2 * pad, ftrH, dim);
}

void renderHomeDialog(App& app, HomeState& H, const Fmts& fm, float W, float top,
                      float bottom) {
    if (H.dialog == HomeDialogKind::None) return;
    const D2DTheme& th = app.theme;
    const bool confirm = H.dialog == HomeDialogKind::ConfirmDelete;
    const bool pasteConflict =
        H.dialog == HomeDialogKind::ConfirmPasteConflicts;
    const float cardW = std::min(dpi(app, pasteConflict ? 430.0f : 460.0f),
                                 W - dpi(app, 56));
    const float cardH =
        dpi(app, pasteConflict ? 200.0f : confirm ? 186.0f : 224.0f);
    const float cardX = (W - cardW) / 2.0f;
    const float cardY = top + std::max(dpi(app, 24), (bottom - top - cardH) / 2.0f);
    const float pad = dpi(app, 20);
    const float radius = dpi(app, 10);

    fill(app, 0, top, W, bottom, D2D1::ColorF(0, 0, 0, 0.30f));
    fillRound(app, cardX, cardY, cardX + cardW, cardY + cardH, radius,
              mix(th.background, th.text, th.isDark ? 0.06f : -0.02f));
    strokeRound(app, cardX + 0.5f, cardY + 0.5f, cardX + cardW - 0.5f,
                cardY + cardH - 0.5f, radius, withAlpha(th.accent, 0.42f), 1.0f);

    const wchar_t* title =
        pasteConflict ? tr(app, "home.paste_conflict_title")
        : H.dialog == HomeDialogKind::NewFile ? tr(app, "home.new_file")
        : H.dialog == HomeDialogKind::NewFolder ? tr(app, "home.new_folder")
        : H.dialog == HomeDialogKind::Rename ? tr(app, "home.rename")
                                             : tr(app, "home.delete");
    text(app, fm.bold, title, cardX + pad, cardY + dpi(app, 14),
         cardW - 2 * pad, dpi(app, 26), th.heading);

    const float buttonsY = cardY + cardH - dpi(app, 48);
    const float primaryW = dpi(app, 104), cancelW = dpi(app, 82);
    const float primaryX = cardX + cardW - pad - primaryW;
    const float cancelX = primaryX - dpi(app, 10) - cancelW;
    H.dialogPrimaryHit = HomeRect{primaryX, buttonsY, primaryX + primaryW,
                                 buttonsY + dpi(app, 30)};
    H.dialogCancelHit = HomeRect{cancelX, buttonsY, cancelX + cancelW,
                                buttonsY + dpi(app, 30)};

    if (pasteConflict) {
        std::wstring names;
        const int shown = std::min(4, (int)H.pasteConflicts.size());
        for (int i = 0; i < shown; i++) {
            if (i) names += L" \u00B7 ";
            names += H.pasteConflicts[i];
        }
        if ((int)H.pasteConflicts.size() > shown)
            names += L"  +" + std::to_wstring(H.pasteConflicts.size() - shown);
        wchar_t lead[512];
        swprintf_s(lead, _countof(lead), tr(app, "home.paste_conflict_lead"),
                   (unsigned)H.pasteConflicts.size(), names.c_str());
        text(app, fm.body, lead, cardX + pad, cardY + dpi(app, 48),
             cardW - 2 * pad, dpi(app, 46), withAlpha(th.text, 0.80f));
        text(app, fm.caption, tr(app, "home.paste_conflict_keys"),
             cardX + pad, cardY + dpi(app, 98), cardW - 2 * pad, dpi(app, 20),
             withAlpha(th.text, 0.45f));
        H.dialogFieldHit = HomeRect{};
    } else if (confirm) {
        wchar_t message[512];
        if (H.dialogTargets.size() > 1)
            swprintf_s(message, _countof(message), tr(app, "home.delete_confirm_many"),
                       (unsigned)H.dialogTargets.size());
        else
            swprintf_s(message, _countof(message), tr(app, "home.delete_confirm"),
                       H.dialogOriginal.c_str());
        text(app, fm.body, message, cardX + pad, cardY + dpi(app, 52),
             cardW - 2 * pad, dpi(app, 62), withAlpha(th.text, 0.78f));
        H.dialogFieldHit = HomeRect{};
    } else {
        const float fieldY = cardY + dpi(app, 50);
        const float fieldH = dpi(app, 36);
        H.dialogFieldHit = HomeRect{cardX + pad, fieldY, cardX + cardW - pad,
                                   fieldY + fieldH};
        fillRound(app, cardX + pad, fieldY, cardX + cardW - pad, fieldY + fieldH,
                  dpi(app, 6), mix(th.background, th.text, th.isDark ? 0.05f : -0.03f));
        strokeRound(app, cardX + pad + 0.5f, fieldY + 0.5f,
                    cardX + cardW - pad - 0.5f, fieldY + fieldH - 0.5f,
                    dpi(app, 6), withAlpha(th.accent, 0.55f), 1.0f);
        const float textX = cardX + pad + dpi(app, 11);
        const int caret = std::clamp(H.dialogCaret, 0, (int)H.dialogText.size());
        const std::wstring before = H.dialogText.substr(0, caret);
        const float caretX = textX + measureText(app, before, fm.mono);
        H.dialogCaretX = caretX;
        H.dialogCaretY = fieldY + fieldH / 2.0f;
        text(app, fm.mono, H.dialogText, textX, fieldY,
             cardW - 2 * pad - dpi(app, 22), fieldH, th.text);
        const float caretTop = fieldY + dpi(app, 8);
        fill(app, caretX, caretTop, caretX + dpi(app, 1.6f),
             fieldY + fieldH - dpi(app, 8), th.accent);

        std::wstring error;
        if (H.dialogError == HomeDialogError::Empty) error = tr(app, "home.name_empty");
        else if (H.dialogError == HomeDialogError::Invalid) error = tr(app, "home.name_invalid");
        else if (H.dialogError == HomeDialogError::Exists) error = tr(app, "home.name_exists");
        else if (H.dialogError == HomeDialogError::Failed) {
            if (H.dialogErrorCode == ERROR_ACCESS_DENIED) {
                error = tr(app, "home.op_denied");
            } else if (H.dialogErrorCode) {
                wchar_t buf[160];
                swprintf_s(buf, _countof(buf), tr(app, "home.op_failed_code"),
                           (unsigned)H.dialogErrorCode);
                error = buf;
            } else {
                error = tr(app, "home.op_failed");
            }
        }
        if (!error.empty())
            text(app, fm.caption, error, cardX + pad, fieldY + fieldH + dpi(app, 8),
                 cardW - 2 * pad, dpi(app, 24), D2D1::ColorF(0.86f, 0.46f, 0.42f));
        else {
            const wchar_t* hint = H.dialog == HomeDialogKind::NewFile
                                      ? tr(app, "home.new_file_hint")
                                      : tr(app, "home.dialog_hint");
            text(app, fm.caption, hint, cardX + pad, fieldY + fieldH + dpi(app, 8),
                 cardW - 2 * pad, dpi(app, 24), withAlpha(th.text, 0.42f));
        }
    }

    const wchar_t* primary =
        pasteConflict ? tr(app, "home.paste_overwrite")
        : confirm ? tr(app, "home.delete")
        : H.dialog == HomeDialogKind::Rename ? tr(app, "home.rename")
                                             : tr(app, "home.create");
    const D2D1_COLOR_F primaryColor =
        (confirm || pasteConflict) ? D2D1::ColorF(0.78f, 0.38f, 0.34f)
                                   : th.accent;
    if (pasteConflict && H.pasteConflictChoice == 0) {
        fillRound(app, cancelX, buttonsY, cancelX + cancelW,
                  buttonsY + dpi(app, 30), dpi(app, 6),
                  mix(th.background, th.accent, 0.16f));
    }
    fillRound(app, primaryX, buttonsY, primaryX + primaryW,
              buttonsY + dpi(app, 30), dpi(app, 6), primaryColor);
    strokeRound(app, cancelX + 0.5f, buttonsY + 0.5f, cancelX + cancelW - 0.5f,
                buttonsY + dpi(app, 30) - 0.5f, dpi(app, 6),
                withAlpha(th.text, 0.22f), 1.0f);
    if (pasteConflict) {
        const float focusX =
            H.pasteConflictChoice == 1 ? primaryX : cancelX;
        const float focusW =
            H.pasteConflictChoice == 1 ? primaryW : cancelW;
        strokeRound(app, focusX - dpi(app, 2), buttonsY - dpi(app, 2),
                    focusX + focusW + dpi(app, 2),
                    buttonsY + dpi(app, 30) + dpi(app, 2), dpi(app, 8),
                    withAlpha(th.accent, 0.95f), 2.0f);
    }
    text(app, fm.button, primary, primaryX, buttonsY, primaryW, dpi(app, 30),
         th.background);
    text(app, fm.button, pasteConflict ? tr(app, "home.paste_skip")
                                       : tr(app, "home.cancel"),
         cancelX, buttonsY, cancelW, dpi(app, 30),
         withAlpha(th.text, pasteConflict && H.pasteConflictChoice == 0 ? 0.95f
                                                                       : 0.78f));
}

void renderHome(App& app) {
    HomeState& H = app.home;
    H.rowHits.clear();
    H.columnHits.clear();
    if (H.dialog != HomeDialogKind::ConfirmDelete &&
        H.dialog != HomeDialogKind::ConfirmPasteConflicts) {
        H.dialogPrimaryHit = HomeRect{};
        H.dialogCancelHit = HomeRect{};
    }
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
    const float hdrH = dpi(app, 48), ftrH = dpi(app, 32);
    const float bodyTop = top + hdrH, bodyBottom = bottom - ftrH;
    const float pad = dpi(app, 18);
    const float rowH = dpi(app, 34);

    Fmts fm;
    fm.left = makeFormat(app, 13.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fm.right = makeFormat(app, 11.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, false);
    fm.bold = makeFormat(app, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fm.body = makeFormat(app, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    fm.caption = makeFormat(app, 11.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fm.mono = makeFormat(app, 13.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false, L"Cascadia Mono");
    fm.tag = makeFormat(app, 9.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, false, L"Cascadia Mono");
    fm.button = makeFormat(app, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, false);

    fill(app, 0, top, W, bottom, th.background);

    // Top bar: repository chip, breadcrumb, search box.
    fill(app, 0, top, W, bodyTop, mix(th.background, th.text, th.isDark ? 0.045f : -0.045f));
    fill(app, 0, bodyTop - 1, W, bodyTop, line);

    std::wstring repoName = dirmodel::leafOf(H.root);
    if (repoName.empty()) repoName = H.root;
    std::wstring repoChip = repoName + L"  \x25BE";
    const float chipH = dpi(app, 30);
    const float chipY = top + (hdrH - chipH) / 2.0f;
    const float chipX = pad;
    const float chipW = measureText(app, repoChip, fm.bold) + dpi(app, 40);
    H.repoChipHit = HomeRect{chipX, chipY, chipX + chipW, chipY + chipH};
    fillRound(app, chipX, chipY, chipX + chipW, chipY + chipH, dpi(app, 6),
              withAlpha(th.accent, 0.10f));
    strokeRound(app, chipX + 0.5f, chipY + 0.5f, chipX + chipW - 0.5f, chipY + chipH - 0.5f,
                dpi(app, 6), withAlpha(th.accent, 0.40f), 1.0f);
    app.brush->SetColor(th.accent);
    app.renderTarget->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(chipX + dpi(app, 12), chipY + chipH / 2.0f),
                      dpi(app, 2.6f), dpi(app, 2.6f)),
        app.brush);
    text(app, fm.bold, repoChip, chipX + dpi(app, 20), chipY, chipW - dpi(app, 26), chipH,
         th.accent);

    const float searchW = std::min(dpi(app, 320), W * 0.36f);
    const float searchX = W - pad - searchW;
    // The chip already names the repository: the breadcrumb only shows the
    // path inside it, so the name is not printed twice.
    std::wstring crumb = tr(app, "home.root");
    if (H.dir.size() > H.root.size()) {
        std::wstring rel = H.dir.substr(H.root.size());
        while (!rel.empty() && (rel[0] == L'\\' || rel[0] == L'/')) rel.erase(0, 1);
        std::wstring pretty;
        for (wchar_t c : rel) {
            if (c == L'\\' || c == L'/') pretty += L" / ";
            else pretty += c;
        }
        if (!pretty.empty()) crumb += L" / " + pretty;
    }
    const float crumbX = chipX + chipW + dpi(app, 18);
    text(app, fm.mono, crumb, crumbX, top, std::max(0.0f, searchX - crumbX - pad), hdrH,
         withAlpha(th.text, 0.60f));

    const D2D1_RECT_F searchRect =
        D2D1::RectF(searchX, chipY, searchX + searchW, chipY + chipH);
    H.searchHit = HomeRect{searchRect.left, searchRect.top, searchRect.right, searchRect.bottom};
    fillRound(app, searchRect.left, searchRect.top, searchRect.right, searchRect.bottom,
              dpi(app, 6), withAlpha(th.text, 0.035f));
    strokeRound(app, searchRect.left + 0.5f, searchRect.top + 0.5f, searchRect.right - 0.5f,
                searchRect.bottom - 0.5f, dpi(app, 6),
                H.searching ? withAlpha(th.accent, 0.65f) : withAlpha(th.text, 0.18f), 1.0f);
    drawSearchIcon(app, searchRect.left + dpi(app, 13), searchRect.top, chipH,
                   H.searching ? th.accent : dim);
    if (H.searching) {
        text(app, fm.mono, L"/ " + H.query + L"\x2502", searchRect.left + dpi(app, 31),
             searchRect.top, searchW - dpi(app, 42), chipH, th.accent);
    } else {
        text(app, fm.mono, tr(app, "home.search"), searchRect.left + dpi(app, 31),
             searchRect.top, searchW - dpi(app, 42), chipH, dim);
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
    const float gap = dpi(app, 16);
    // The responsive rules are written in DIPs. app.width is physical pixels,
    // so convert before choosing widths: otherwise a 150% display would make
    // an "800 DIP" column only 533 DIP wide.
    const float uiScale = app.contentScale > 0.01f ? app.contentScale : 1.0f;
    const HomeColumnsLayout layout =
        homeColumnsLayout(W / uiScale, count, pad / uiScale, gap / uiScale);
    const float colW = layout.width * uiScale;
    const float startX = layout.startX * uiScale;
    const float radius = dpi(app, 9);
    const float headerH = dpi(app, 32);
    const float rowsTop = bodyTop + headerH + dpi(app, 4);
    const int visibleRows = std::max(1, (int)((bodyBottom - rowsTop) / rowH));
    H.visibleRows = visibleRows;

    const float badgeW = dpi(app, 28), badgeH = dpi(app, 17), leadW = dpi(app, 36);
    const float rightPad = dpi(app, 18);
    const float sizeW = dpi(app, 64), whenW = dpi(app, 90);
    const bool showMeta = colW > dpi(app, 360);
    const float metaW = showMeta ? sizeW + whenW + dpi(app, 20) : 0.0f;
    const float nameW =
        std::max(dpi(app, 24),
                 colW - pad - rightPad - leadW - metaW - dpi(app, 8));

    for (int ci = 0; ci < count; ci++) {
        const ColRef& col = cols[ci];
        const float x = startX + ci * (colW + gap);
        const bool active = col.active;
        const bool inlineNew =
            active && (H.dialog == HomeDialogKind::NewFile ||
                       H.dialog == HomeDialogKind::NewFolder);
        const int colVisible = std::max(1, visibleRows - (inlineNew ? 1 : 0));
        // A soft shadow and a rounded, slightly lifted surface make the
        // columns read as sheets instead of flat boxes.
        const D2D1_COLOR_F shadow = th.isDark ? D2D1::ColorF(0, 0, 0, 0.30f)
                                              : D2D1::ColorF(0, 0, 0, 0.07f);
        fillRound(app, x + dpi(app, 1), bodyTop + dpi(app, 3), x + colW + dpi(app, 1),
                  bodyBottom + dpi(app, 1), radius, shadow);
        const float tint = th.isDark ? (active ? 0.050f : 0.024f)
                                     : (active ? -0.030f : -0.014f);
        fillRound(app, x, bodyTop, x + colW, bodyBottom, radius,
                  mix(th.background, th.text, tint));
        strokeRound(app, x + 0.5f, bodyTop + 0.5f, x + colW - 0.5f, bodyBottom - 0.5f,
                    radius, active ? withAlpha(th.accent, 0.48f) : withAlpha(th.text, 0.14f),
                    active ? 1.25f : 1.0f);
        fill(app, x + pad, bodyTop + headerH, x + colW - pad, bodyTop + headerH + 1.0f,
             withAlpha(th.text, 0.08f));

        std::wstring colName = (*col.path == H.root) ? repoName : dirmodel::leafOf(*col.path);
        if (colName.empty()) colName = *col.path;
        wchar_t countBuf[64];
        swprintf_s(countBuf, _countof(countBuf),
                   tr(app, col.view->size() == 1 ? "home.item" : "home.items"),
                   (unsigned)col.view->size());
        // A single column is already identified by the breadcrumb: keep its
        // header quiet and show only the item count there.
        const std::wstring colHead =
            count > 1 ? colName + L"  \x00B7  " + countBuf : std::wstring(countBuf);
        text(app, fm.caption, colHead, x + pad, bodyTop, colW - pad * 2, headerH,
             (count > 1 && active) ? th.accent : withAlpha(th.text, 0.58f));

        int topRow = col.topRow;
        int maxTop = std::max(0, (int)col.view->size() - colVisible);
        topRow = std::clamp(topRow, 0, maxTop);
        if (active && col.sel >= 0) {
            if (col.sel < topRow) topRow = col.sel;
            else if (col.sel >= topRow + colVisible) topRow = col.sel - colVisible + 1;
            topRow = std::clamp(topRow, 0, maxTop);
            H.topRow = topRow;
        }

        float y = rowsTop;
        if (inlineNew) {
            fill(app, x + 1, y, x + colW - 1, y + rowH,
                 mix(th.background, th.accent, th.isDark ? 0.14f : 0.10f));
            const wchar_t* tag =
                H.dialog == HomeDialogKind::NewFolder ? L"DIR" : L"MD";
            text(app, fm.tag, tag, x + pad, y, badgeW, rowH, th.accent);
            inlineNameField(app, H, fm, x + pad + leadW, y + dpi(app, 2),
                            nameW, rowH - dpi(app, 4), th);
            y += rowH;
        }
        if (col.view->empty() && !inlineNew) {
            std::wstring msg;
            if (!col.listing->ok)
                msg = tr(app, "home.read_failed");
            else if (!H.query.empty())
                msg = tr(app, "home.no_matches");
            else
                msg = tr(app, "home.empty_folder");
            if (col.listing->ok) {
                msg += L"  \x00B7  ";
                msg += tr(app, H.query.empty() ? "home.empty_hint"
                                               : "home.clear_hint");
            }
            text(app, fm.caption, msg, x + pad + leadW, y,
                 colW - pad * 2 - leadW, rowH, withAlpha(th.text, 0.45f));
        }
        for (int n = 0; n < colVisible; n++) {
            int row = topRow + n;
            if (row >= (int)col.view->size()) break;
            const Entry& e = col.listing->entries[(*col.view)[row]];
            const bool selected = row == col.sel;
            const bool editingName =
                active && H.dialog == HomeDialogKind::Rename && row == col.sel;
            const bool multi = active && homeIsSelected(H, e.name);
            const bool hovered = ci == H.hoverColumn && row == H.hoverRow;
            // Cursor and marked selection are different states: the cursor is
            // a neutral row with an accent outline; marked rows get the accent
            // fill and rail. A 1px inset keeps adjacent marked rows distinct.
            const float rowL = x + dpi(app, 2);
            const float rowR = x + colW - dpi(app, 2);
            const float rowT = y + dpi(app, 1);
            const float rowB = y + rowH - dpi(app, 1);
            const float rowRadius = dpi(app, 4);
            if (multi) {
                fillRound(app, rowL, rowT, rowR, rowB, rowRadius,
                          mix(th.background, th.accent,
                              th.isDark ? 0.26f : 0.18f));
                fillRound(app, rowL, rowT + dpi(app, 3), rowL + dpi(app, 3),
                          rowB - dpi(app, 3), dpi(app, 1.5f), th.accent);
            } else if (selected) {
                fillRound(app, rowL, rowT, rowR, rowB, rowRadius,
                          mix(th.background, th.text,
                              th.isDark ? 0.09f : 0.06f));
                strokeRound(app, rowL + 0.5f, rowT + 0.5f, rowR - 0.5f,
                            rowB - 0.5f, rowRadius,
                            withAlpha(th.accent, 0.55f), 1.0f);
            } else if (hovered) {
                fillRound(app, rowL, rowT, rowR, rowB, rowRadius,
                          mix(th.background, th.text,
                              th.isDark ? 0.10f : 0.075f));
            }
            if (selected && multi) {
                strokeRound(app, rowL + 0.5f, rowT + 0.5f, rowR - 0.5f,
                            rowB - 0.5f, rowRadius,
                            withAlpha(th.accent, 0.95f), 1.4f);
            }

            // Folder chevron or file-type badge in a fixed leading slot.
            const float badgeX = x + pad;
            const float badgeY = y + (rowH - badgeH) / 2.0f;
            if (e.kind == Kind::Folder) {
                text(app, fm.tag, L"\x25B8", badgeX, y, badgeW, rowH, th.accent);
            } else {
                const D2D1_COLOR_F tagBg =
                    withAlpha(e.kind == Kind::Document ? th.accent : th.text, 0.10f);
                const D2D1_COLOR_F tagFg =
                    e.kind == Kind::Document ? th.accent : withAlpha(th.text, 0.55f);
                fillRound(app, badgeX, badgeY, badgeX + badgeW, badgeY + badgeH,
                          dpi(app, 3), tagBg);
                text(app, fm.tag, fileTypeTag(e), badgeX, y, badgeW, rowH, tagFg);
            }

            if (editingName) {
                inlineNameField(app, H, fm, x + pad + leadW, y + dpi(app, 2),
                                nameW, rowH - dpi(app, 4), th);
            } else {
                text(app, fm.mono, entryLabel(e), x + pad + leadW, y, nameW, rowH,
                     entryColor(th, e, selected || multi));
            }
            if (showMeta) {
                // Name > modified > size: three brightness levels instead of
                // one flat dim tone for all metadata.
                const bool strong = selected || multi;
                const D2D1_COLOR_F sizeC = withAlpha(th.text, strong ? 0.56f : 0.36f);
                const D2D1_COLOR_F whenC = withAlpha(th.text, strong ? 0.66f : 0.48f);
                const float whenX = x + colW - rightPad - whenW;
                const float sizeX = whenX - sizeW - dpi(app, 10);
                if (e.kind != Kind::Folder)
                    text(app, fm.right, dirmodel::formatSize(e.size), sizeX, y, sizeW, rowH,
                         sizeC);
                text(app, fm.right, formatWhen(e.mtime), whenX, y, whenW, rowH, whenC);
            }
            H.columnHits.push_back({HomeRect{x, y, x + colW, y + rowH}, ci, row});
            y += rowH;
        }

        const float railH = std::max(
            0.0f, std::min((float)visibleRows * rowH, bodyBottom - rowsTop) - dpi(app, 8));
        drawColumnScroll(app, x + colW, rowsTop + dpi(app, 4), railH,
                         (int)col.view->size(), visibleRows, topRow,
                         withAlpha(th.text, 0.07f),
                         withAlpha(active ? th.accent : th.text, active ? 0.55f : 0.28f));
    }

    // Footer: selected item on the left, key hints on the right.
    fill(app, 0, bodyBottom, W, bottom, mix(th.background, th.text, th.isDark ? 0.05f : -0.04f));
    fill(app, 0, bodyBottom, W, bodyBottom + 1, line);
    if (!H.repoNotice.empty()) {
        text(app, fm.caption, H.repoNotice, pad, bodyBottom, W - 2 * pad, ftrH,
             D2D1::ColorF(0.86f, 0.46f, 0.42f));
    } else if (!H.fileNotice.empty()) {
        text(app, fm.caption, H.fileNotice, pad, bodyBottom, W - 2 * pad, ftrH,
             withAlpha(th.accent, 0.82f));
    } else {
        std::wstring info;
        D2D1_COLOR_F infoColor = withAlpha(th.text, 0.68f);
        const bool inlineEdit =
            H.dialog == HomeDialogKind::NewFile ||
            H.dialog == HomeDialogKind::NewFolder ||
            H.dialog == HomeDialogKind::Rename;
        const size_t selectedCount = homeSelectionCount(H);
        if (inlineEdit) {
            if (H.dialogError == HomeDialogError::Empty) {
                info = tr(app, "home.name_empty");
            } else if (H.dialogError == HomeDialogError::Invalid) {
                info = tr(app, "home.name_invalid");
            } else if (H.dialogError == HomeDialogError::Exists) {
                info = tr(app, "home.name_exists");
            } else if (H.dialogError == HomeDialogError::Failed) {
                if (H.dialogErrorCode == ERROR_ACCESS_DENIED) {
                    info = tr(app, "home.op_denied");
                } else if (H.dialogErrorCode) {
                    wchar_t buf[160];
                    swprintf_s(buf, _countof(buf), tr(app, "home.op_failed_code"),
                               (unsigned)H.dialogErrorCode);
                    info = buf;
                } else {
                    info = tr(app, "home.op_failed");
                }
            } else {
                info = tr(app, "home.dialog_hint");
            }
            if (H.dialogError != HomeDialogError::None)
                infoColor = D2D1::ColorF(0.86f, 0.46f, 0.42f);
        } else if (selectedCount > 0) {
            wchar_t selectedLabel[64];
            swprintf_s(selectedLabel, _countof(selectedLabel),
                       tr(app, "home.selected"), (unsigned)selectedCount);
            info = selectedLabel;
        } else if (const Entry* e = homeSelected(H)) {
            info = e->kind == Kind::Folder
                       ? e->name + L"  \x00B7  " + tr(app, "home.folder")
                       : e->name + L"  \x00B7  " + dirmodel::formatSize(e->size);
        }
        if (!inlineEdit && !H.clipboard.empty()) {
            wchar_t clip[64];
            swprintf_s(clip, _countof(clip),
                       tr(app, H.clipboardCut ? "home.clipboard_cut"
                                              : "home.clipboard_copy"),
                       (unsigned)H.clipboard.size());
            if (!info.empty()) info += L"  \x00B7  ";
            info += clip;
        }
        const std::wstring hints =
            selectedCount > 0
                ? std::wstring(tr(app, "home.footer_selected"))
                : (!H.clipboard.empty()
                       ? std::wstring(tr(app, "home.footer_clipboard"))
                       : std::wstring(tr(app, "home.footer")));
        const float hintW = measureText(app, hints, fm.right) + dpi(app, 6);
        const bool showHints = !inlineEdit && W - 2 * pad > hintW + dpi(app, 220);
        if (showHints) {
            text(app, fm.right, hints, W - pad - hintW, bodyBottom, hintW, ftrH,
                 withAlpha(th.text, 0.42f));
        }
        const float infoW =
            W - 2 * pad - (showHints ? hintW + dpi(app, 24) : 0.0f);
        text(app, fm.caption, info, pad, bodyBottom, std::max(0.0f, infoW), ftrH,
             infoColor);
    }
    if (H.dialog == HomeDialogKind::ConfirmDelete ||
        H.dialog == HomeDialogKind::ConfirmPasteConflicts)
        renderHomeDialog(app, H, fm, W, top, bottom);
    fm.release();
}
