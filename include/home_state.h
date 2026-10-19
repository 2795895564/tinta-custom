#pragma once
// State and navigation logic of the Home view (the repo file manager).
// Platform-free on purpose: the shell (homeview.cpp) feeds it keys and
// scan results, then executes the side effects it leaves behind
// (pendingOpen, need). That split keeps the logic unit-testable and keeps
// App from growing a dozen more fields.
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "dirmodel.h"

struct HomeRect { float l = 0, t = 0, r = 0, b = 0; };

// Responsive Miller-column geometry (in DIPs). Kept platform-free so the
// 800/900 single-column rule, the 520/400 multi-column rules and the
// "fill the window when it gets narrow" behaviour stay testable.
struct HomeColumnsLayout {
    float width = 0;       // width of every column
    float totalWidth = 0;  // columns + gaps
    float startX = 0;      // centered group
};
HomeColumnsLayout homeColumnsLayout(float viewWidth, int count, float pad, float gap);

enum HomeSlot { kHomeParent = 0, kHomeCurrent = 1, kHomePreviewDir = 2, kHomePreviewFile = 3 };

enum class HomeScreen { Home, Picker };

// One line of the repo picker (wide strings: they are drawn and used as paths)
struct RepoRow {
    std::string id;
    std::wstring name, path;
    unsigned long long lastOpened = 0;
    bool missing = false;  // the folder no longer exists
    bool current = false;  // shown as the startup/default repository
};

// One visible line in the Home B folder tree
struct HomeTreeRow {
    std::wstring path;
    std::wstring name;
    int depth = 0;
    bool folder = false;
    bool expanded = false;
};

// One visible column in the Home B column browser (left of the active one)
struct HomeColumn {
    std::wstring path;
    dirmodel::Listing listing;
    std::vector<int> view;
    int sel = 0;
    int topRow = 0;
};

struct HomeColumnHit {
    HomeRect rect;
    int column = 0;
    int row = 0;
};

// One small modal for Home file operations. Name dialogs keep a caret and
// validate Windows file-name rules; confirm-delete has no text field.
enum class HomeDialogKind {
    None,
    NewFile,
    NewFolder,
    Rename,
    ConfirmDelete,
    ConfirmPasteConflicts
};
enum class HomeDialogError { None, Empty, Invalid, Exists, Failed };

struct HomeTransfer {
    std::wstring source;
    std::wstring target;
};

struct HomeState {
    std::wstring root;  // repo root: navigation never leaves it
    std::wstring dir;   // folder shown in the middle column
    dirmodel::Options opts;
    dirmodel::Listing parent, current, previewDir;
    std::wstring previewTarget;  // full path being previewed, "" = none
    std::wstring previewText;    // head of the previewed file
    bool previewIsDir = false;

    std::vector<int> view;  // indices into current.entries after filter/rank
    int sel = 0;            // position in view
    int topRow = 0;
    int visibleRows = 20;   // written by the renderer each frame
    // Multi-select for the active directory: lowercase names survive rescans.
    std::set<std::wstring> selected;
    // Internal file clipboard: absolute source paths, cut flag, and the shell
    // effect requested by Ctrl+V.
    std::vector<std::wstring> clipboard;
    bool clipboardCut = false;
    bool pendingPaste = false;
    std::vector<std::wstring> pasteConflicts;
    int pasteConflictChoice = 0;  // 0 = skip, 1 = overwrite
    // Last successful paste, so Ctrl+Z can put moves back or remove copies.
    std::vector<HomeTransfer> undoTransfers;
    bool undoIsMove = false;
    bool pendingUndo = false;

    std::wstring query;
    bool searching = false;
    wchar_t swallow = 0;    // the char the '/' keydown is about to post

    // Home file-operation modal (new file/folder, rename, delete confirm).
    HomeDialogKind dialog = HomeDialogKind::None;
    std::wstring dialogText;      // name being typed
    std::wstring dialogOriginal;  // rename/delete source name
    std::vector<std::wstring> dialogTargets;  // delete selection
    int dialogHover = -1;  // 0 = cancel/skip, 1 = primary
    std::wstring dialogParent;    // directory the operation applies to
    int dialogCaret = 0;
    HomeDialogError dialogError = HomeDialogError::None;
    unsigned long dialogErrorCode = 0;  // Win32/HRESULT detail for Failed
    bool dialogCommit = false;    // shell executes, then clears
    HomeRect dialogFieldHit, dialogPrimaryHit, dialogCancelHit;
    float dialogCaretX = 0, dialogCaretY = 0;  // client-space IME anchor

    std::wstring restoreName;                       // select this when the listing lands
    std::map<std::wstring, std::wstring> lastPick;  // folder (lowercase) -> last selected name
    bool loading = false;

    // Side effects for the shell
    std::wstring pendingOpen;  // open this file in the viewer
    unsigned need = 0;         // bit i = slot i wants a (re)scan
    uint32_t gen[4] = {0, 0, 0, 0};  // stale scan results are dropped

    // Repo picker. The shell fills repoRows from settings and runs the
    // pending* side effects (it owns the dialogs and settings.ini).
    HomeScreen screen = HomeScreen::Home;
    bool booted = false;   // the startup decision has run
    std::string repoId;    // repo shown in Home ("" = none)
    std::vector<RepoRow> repoRows;
    int repoSel = 0;
    std::wstring repoNotice;
    std::wstring fileNotice;  // transient success/info from file operations
    bool refreshRows = false;
    bool pendingBrowse = false;
    std::string pendingChoose, pendingRemove, pendingRelocate, pendingDefault;
    std::string confirmRemove;  // X once arms, X again forgets
    HomeRect browseBtn;
    bool hasBrowseBtn = false;

    // Renderer / mouse scratch
    int hoverRow = -1;
    std::vector<std::pair<HomeRect, int>> rowHits;  // rect -> view index
    unsigned long long lastClickTick = 0;
    int lastClickRow = -1;

    // Home B left folder tree. The cache is populated lazily when a folder
    // is expanded; treeRows is the flattened visible projection.
    std::wstring treeRoot;
    std::vector<HomeTreeRow> treeRows;
    std::set<std::wstring> expandedDirs;
    std::map<std::wstring, dirmodel::Listing> treeCache;
    int treeSel = -1;
    int treeHover = -1;
    int treeTopRow = 0;
    int treeVisibleRows = 20;
    std::vector<std::pair<HomeRect, int>> treeHits;
    std::vector<std::pair<HomeRect, int>> treeArrowHits;

    // Home B top bar
    HomeRect repoChipHit;
    HomeRect searchHit;

    // Home B Miller columns: left columns are snapshots; the active column
    // stays in dir/current/view/sel/topRow. At most two columns are kept
    // left of the active one, so the total never exceeds three.
    std::wstring columnsRoot;
    std::vector<HomeColumn> columns;
    std::vector<HomeColumnHit> columnHits;
    int hoverColumn = -1;
    int lastClickColumn = -1;
};

bool homeHasRoot(const HomeState& s);
void homeSetRoot(HomeState& s, const std::wstring& root, const std::wstring& startDir);
void homeNavigate(HomeState& s, const std::wstring& dir, const std::wstring& select);
void homeApplyListing(HomeState& s, int slot, uint32_t gen, dirmodel::Listing&& listing);
void homeApplyPreviewText(HomeState& s, uint32_t gen, const std::wstring& path,
                          std::wstring&& text);
const dirmodel::Entry* homeSelected(const HomeState& s);
size_t homeSelectionCount(const HomeState& s);
bool homeIsSelected(const HomeState& s, const std::wstring& name);
void homeMoveTo(HomeState& s, int index);
void homeMove(HomeState& s, int delta);
void homeActivate(HomeState& s);
void homeUp(HomeState& s);
bool homeKey(HomeState& s, unsigned vk, bool ctrl, bool shift);  // true = consumed
bool homeChar(HomeState& s, wchar_t c);                          // true = consumed
void homeDialogClose(HomeState& s);
unsigned homeTakeRequests(HomeState& s);

void homeSetRepoRows(HomeState& s, std::vector<RepoRow>&& rows);
void homeShowPicker(HomeState& s);
void homeChooseRepo(HomeState& s, int index);  // sets pendingChoose, or a notice if missing
