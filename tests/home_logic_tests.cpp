// Pure-logic tests for the Home view: build with any C++17 compiler.
#include <cstdio>
#include "home_state.h"
using namespace dirmodel;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static Entry E(const wchar_t* n, Kind k, uint64_t size = 0, uint64_t mt = 0, bool hid = false) {
    Entry e; e.name = n; e.kind = k; e.size = size; e.mtime = mt; e.hidden = hid; return e;
}
static Listing L(const std::wstring& path, std::vector<Entry> es) {
    Listing l; l.path = path; l.entries = std::move(es); l.ok = true; return l;
}
static void land(HomeState& s, int slot, Listing l) {
    homeApplyListing(s, slot, s.gen[slot], std::move(l));
}

int main() {
    // paths
    CHECK(isRoot(L"C:\\") && isRoot(L"C:") && !isRoot(L"C:\\a"));
    CHECK(join(L"C:\\a", L"b.md") == L"C:\\a\\b.md" && join(L"C:\\", L"b") == L"C:\\b");
    CHECK(parentOf(L"C:\\a\\b") == L"C:\\a" && parentOf(L"C:\\a") == L"C:\\" && parentOf(L"C:\\") == L"C:\\");
    CHECK(leafOf(L"C:\\a\\b\\") == L"b");
    CHECK(isInside(L"C:\\notes", L"c:/NOTES/x") && !isInside(L"C:\\notes", L"C:\\notes2"));

    // natural sort, folders first, descending keeps grouping
    CHECK(naturalCompare(L"2.md", L"10.md") < 0 && naturalCompare(L"A.md", L"a.md") == 0);
    std::vector<Entry> v = {E(L"10.md", Kind::Document, 5), E(L"z", Kind::Folder), E(L"2.md", Kind::Document, 9), E(L"a", Kind::Folder)};
    Options o; sortEntries(v, o);
    CHECK(v[0].name == L"a" && v[1].name == L"z" && v[2].name == L"2.md" && v[3].name == L"10.md");
    o.key = SortKey::Size; o.descending = true; sortEntries(v, o);
    CHECK(v[0].kind == Kind::Folder && v[2].name == L"2.md");

    // fuzzy + visible
    CHECK(fuzzyScore(L"design-spec.md", L"dsp") > 0 && fuzzyScore(L"todo.md", L"zzz") < 0);
    CHECK(fuzzyScore(L"todo.md", L"todo") > fuzzyScore(L"my-todo.md", L"todo"));
    std::vector<Entry> w = {E(L".git", Kind::Folder, 0, 0, true), E(L"a.md", Kind::Document), E(L"p.png", Kind::Other)};
    Options ho; CHECK(visible(w, ho, L"").size() == 2);
    ho.showHidden = true; CHECK(visible(w, ho, L"").size() == 3);
    ho.showOther = false; CHECK(visible(w, ho, L"").size() == 2);
    CHECK(formatSize(3174) == L"3.1 KB" && formatSize(512) == L"512 B" && formatSize(2 * 1024 * 1024) == L"2 MB");

    // navigation
    HomeState s;
    homeSetRoot(s, L"C:\\notes", L"");
    CHECK(s.dir == L"C:\\notes" && (s.need & 2) && !(s.need & 1));  // current only at root
    unsigned need = homeTakeRequests(s); CHECK(need == 2 && s.need == 0);
    homeApplyListing(s, kHomeCurrent, 999, L(L"C:\\notes", {E(L"x.md", Kind::Document)}));
    CHECK(s.view.empty());  // stale generation ignored
    land(s, kHomeCurrent, L(L"C:\\notes", {E(L"x.md", Kind::Document), E(L"proj", Kind::Folder), E(L"y.png", Kind::Other), E(L"b.md", Kind::Document)}));
    CHECK(s.view.size() == 4 && homeSelected(s)->name == L"proj");
    CHECK(s.need & (1u << kHomePreviewDir));  // folder selected -> preview scan requested
    homeTakeRequests(s);
    homeKey(s, 'J', false, false);
    CHECK(homeSelected(s)->name == L"b.md" && (s.need & (1u << kHomePreviewFile)));
    homeKey(s, 'J', false, false); homeKey(s, 'J', false, false);
    CHECK(homeSelected(s)->name == L"y.png");
    homeKey(s, 0x0D, false, false);
    CHECK(s.pendingOpen.empty());  // non-Markdown is not opened
    homeKey(s, 'K', false, false);
    homeKey(s, 0x0D, false, false);
    CHECK(s.pendingOpen == L"C:\\notes\\x.md" || s.pendingOpen == L"C:\\notes\\b.md");
    s.pendingOpen.clear();

    // into folder, then back restores the folder row; root is a wall
    homeKey(s, 'G', false, false);  // top = proj
    homeKey(s, 'L', false, false);
    CHECK(s.dir == L"C:\\notes\\proj" && (s.need & 1) && (s.need & 2));
    homeTakeRequests(s);
    land(s, kHomeCurrent, L(L"C:\\notes\\proj", {E(L"a.md", Kind::Document), E(L"c.md", Kind::Document)}));
    homeMove(s, 1);
    homeKey(s, 'H', false, false);
    CHECK(s.dir == L"C:\\notes");
    land(s, kHomeCurrent, L(L"C:\\notes", {E(L"x.md", Kind::Document), E(L"proj", Kind::Folder)}));
    CHECK(homeSelected(s)->name == L"proj");
    homeKey(s, 'H', false, false);
    CHECK(s.dir == L"C:\\notes");  // cannot leave the repo
    homeNavigate(s, L"C:\\other", L"");
    CHECK(s.dir == L"C:\\notes");
    homeKey(s, 'L', false, false);  // re-enter: remembers c.md
    land(s, kHomeCurrent, L(L"C:\\notes\\proj", {E(L"a.md", Kind::Document), E(L"c.md", Kind::Document)}));
    CHECK(homeSelected(s)->name == L"c.md");

    // search typing is swallowed from shortcuts; '/' echo is dropped
    CHECK(homeKey(s, 0xBF, false, false) && s.searching);
    CHECK(homeChar(s, L'/') && s.query.empty());
    CHECK(homeKey(s, 'T', false, false));  // eaten, no app shortcut
    CHECK(homeChar(s, L'c') && s.query == L"c" && homeSelected(s)->name == L"c.md" && s.view.size() == 1);
    CHECK(homeKey(s, 0x1B, false, false) && !s.searching && s.query.empty() && s.view.size() == 2);
    CHECK(!homeKey(s, 'O', true, false));  // Ctrl chords untouched
    CHECK(!homeChar(s, L'x'));             // not searching: char not consumed

    // hidden toggle
    land(s, kHomeCurrent, L(L"C:\\notes\\proj", {E(L".h", Kind::Document, 0, 0, true), E(L"a.md", Kind::Document)}));
    CHECK(s.view.size() == 1);
    homeKey(s, 0xBE, false, false);
    CHECK(s.view.size() == 2);

    // repo picker
    HomeState p;
    std::vector<RepoRow> rows(3);
    rows[0].id = "r1"; rows[0].name = L"notes"; rows[0].path = L"C:\\notes";
    rows[1].id = "r2"; rows[1].name = L"docs";  rows[1].path = L"D:\\docs";
    rows[2].id = "r3"; rows[2].name = L"gone";  rows[2].path = L"E:\\gone"; rows[2].missing = true;
    homeShowPicker(p); homeSetRepoRows(p, std::move(rows));
    CHECK(p.screen == HomeScreen::Picker && p.refreshRows);
    CHECK(homeKey(p, '2', false, false) && p.pendingChoose == "r2" && p.repoSel == 1);
    p.pendingChoose.clear();
    CHECK(homeKey(p, '3', false, false) && p.pendingChoose.empty() && !p.repoNotice.empty());  // missing: notice only
    CHECK(homeKey(p, '9', false, false) && p.pendingChoose.empty());                            // no such row: eaten
    CHECK(homeKey(p, 'J', false, false) && p.repoSel == 2 && homeKey(p, 'J', false, false) && p.repoSel == 2);
    CHECK(homeKey(p, 'K', false, false) && p.repoSel == 1);
    p.repoNotice.clear();
    CHECK(homeKey(p, 0x0D, false, false) && p.pendingChoose == "r2");
    CHECK(homeKey(p, 'O', true, false) && p.pendingBrowse);       // Ctrl+O: add folder
    p.pendingBrowse = false;
    CHECK(homeKey(p, 'O', false, false) && p.pendingBrowse);
    CHECK(homeKey(p, 'X', false, false) && p.pendingRemove == "r2");
    CHECK(!homeKey(p, 'T', true, false));                          // other Ctrl chords untouched
    CHECK(!homeKey(p, 0x1B, false, false));                        // no repo open: Esc not ours
    homeSetRoot(p, L"C:\\notes", L""); homeShowPicker(p);
    CHECK(homeKey(p, 0x1B, false, false) && p.screen == HomeScreen::Home);  // Esc goes back to the repo
    CHECK(homeKey(p, 'O', false, false) && p.screen == HomeScreen::Picker && p.refreshRows);  // 'o' in Home opens the picker
    homeSetRepoRows(p, {});
    CHECK(p.repoRows.empty() && p.repoSel == 0);

    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
