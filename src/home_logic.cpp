#include "home_state.h"

#include <algorithm>
#include <cwctype>

using namespace dirmodel;

namespace {

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

bool sameDir(const std::wstring& a, const std::wstring& b) {
    return isInside(a, b) && isInside(b, a);
}

void request(HomeState& s, int slot) {
    s.gen[slot]++;
    s.need |= 1u << slot;
}

void clearPreview(HomeState& s) {
    s.previewTarget.clear();
    s.previewText.clear();
    s.previewDir = Listing{};
    s.previewIsDir = false;
}

void ensureVisible(HomeState& s) {
    int rows = std::max(1, s.visibleRows);
    if (s.sel < s.topRow) s.topRow = s.sel;
    else if (s.sel >= s.topRow + rows) s.topRow = s.sel - rows + 1;
    s.topRow = std::max(0, s.topRow);
}

void updatePreview(HomeState& s) {
    const Entry* e = homeSelected(s);
    if (!e) { clearPreview(s); return; }
    std::wstring target = join(s.dir, e->name);
    if (target == s.previewTarget) return;
    clearPreview(s);
    s.previewTarget = target;
    if (e->kind == Kind::Folder) {
        s.previewIsDir = true;
        request(s, kHomePreviewDir);
    } else if (e->kind == Kind::Document) {
        request(s, kHomePreviewFile);
    }
}

void remember(HomeState& s) {
    if (s.searching || s.dir.empty()) return;
    if (const Entry* e = homeSelected(s)) s.lastPick[lower(s.dir)] = e->name;
}

void rebuildView(HomeState& s, const std::wstring& prefer) {
    std::wstring keep = prefer;
    if (keep.empty())
        if (const Entry* e = homeSelected(s)) keep = e->name;
    s.view = visible(s.current.entries, s.opts, s.query);
    int found = -1;
    for (int i = 0; i < (int)s.view.size(); i++)
        if (s.current.entries[s.view[i]].name == keep) { found = i; break; }
    if (found >= 0) s.sel = found;
    else s.sel = s.view.empty() ? 0 : std::min(s.sel, (int)s.view.size() - 1);
    ensureVisible(s);
    updatePreview(s);
    remember(s);
}

// Typing changes the ranking: jump to the best match, not the old row
void refilter(HomeState& s) {
    rebuildView(s, L"");
    s.sel = 0;
    s.topRow = 0;
    updatePreview(s);
}

}  // namespace

bool homeHasRoot(const HomeState& s) { return !s.root.empty(); }

void homeSetRoot(HomeState& s, const std::wstring& root, const std::wstring& startDir) {
    s.root = root;
    s.lastPick.clear();
    s.parent = Listing{};
    s.current = Listing{};
    s.dir.clear();
    homeNavigate(s, !startDir.empty() && isInside(root, startDir) ? startDir : root, L"");
}

void homeNavigate(HomeState& s, const std::wstring& dir, const std::wstring& select) {
    if (s.root.empty() || !isInside(s.root, dir)) return;
    s.dir = dir;
    s.query.clear();
    s.searching = false;
    s.restoreName = select;
    s.current = Listing{};
    s.view.clear();
    s.sel = s.topRow = 0;
    s.loading = true;
    clearPreview(s);
    request(s, kHomeCurrent);
    if (sameDir(dir, s.root)) s.parent = Listing{};
    else request(s, kHomeParent);
}

void homeApplyListing(HomeState& s, int slot, uint32_t gen, Listing&& l) {
    if (slot < 0 || slot > 2 || gen != s.gen[slot]) return;  // stale
    if (slot == kHomeParent) {
        if (l.path != parentOf(s.dir)) return;
        sortEntries(l.entries, s.opts);
        s.parent = std::move(l);
    } else if (slot == kHomeCurrent) {
        if (l.path != s.dir) return;
        sortEntries(l.entries, s.opts);
        s.current = std::move(l);
        s.loading = false;
        std::wstring prefer = s.restoreName;
        s.restoreName.clear();
        if (prefer.empty()) {
            auto it = s.lastPick.find(lower(s.dir));
            if (it != s.lastPick.end()) prefer = it->second;
        }
        rebuildView(s, prefer);
    } else {
        if (l.path != s.previewTarget) return;
        sortEntries(l.entries, s.opts);
        s.previewDir = std::move(l);
    }
}

void homeApplyPreviewText(HomeState& s, uint32_t gen, const std::wstring& path,
                          std::wstring&& text) {
    if (gen != s.gen[kHomePreviewFile] || path != s.previewTarget) return;
    s.previewText = std::move(text);
}

const Entry* homeSelected(const HomeState& s) {
    if (s.sel < 0 || s.sel >= (int)s.view.size()) return nullptr;
    int idx = s.view[s.sel];
    if (idx < 0 || idx >= (int)s.current.entries.size()) return nullptr;
    return &s.current.entries[idx];
}

void homeMoveTo(HomeState& s, int index) {
    if (s.view.empty()) return;
    s.sel = std::clamp(index, 0, (int)s.view.size() - 1);
    ensureVisible(s);
    updatePreview(s);
    remember(s);
}

void homeMove(HomeState& s, int delta) { homeMoveTo(s, s.sel + delta); }

void homeActivate(HomeState& s) {
    const Entry* e = homeSelected(s);
    if (!e) return;
    if (e->kind == Kind::Folder) homeNavigate(s, join(s.dir, e->name), L"");
    else if (e->kind == Kind::Document) s.pendingOpen = join(s.dir, e->name);
}

void homeUp(HomeState& s) {
    if (s.root.empty() || sameDir(s.dir, s.root)) return;
    homeNavigate(s, parentOf(s.dir), leafOf(s.dir));
}

// Picker keys: 1-9 open that repo, arrows/jk + Enter, o or Ctrl+O adds a
// folder, x/Delete forgets a repo (the folder on disk is never touched).
static bool pickerKey(HomeState& s, unsigned vk, bool ctrl) {
    enum : unsigned { RET = 0x0D, ESC = 0x1B, UP = 0x26, DOWN = 0x28, DEL = 0x2E };
    if (ctrl) {
        if (vk != 'O') return false;
        s.pendingBrowse = true;
        return true;
    }
    int n = (int)s.repoRows.size();
    int digit = (vk >= '1' && vk <= '9') ? (int)(vk - '1')
                : (vk >= 0x61 && vk <= 0x69) ? (int)(vk - 0x61) : -1;
    if (digit >= 0) {
        if (digit < n) { s.repoSel = digit; homeChooseRepo(s, digit); }
        return true;
    }
    switch (vk) {
        case DOWN: case 'J': if (n) s.repoSel = std::min(s.repoSel + 1, n - 1); return true;
        case UP: case 'K': s.repoSel = std::max(s.repoSel - 1, 0); return true;
        case RET: if (n) homeChooseRepo(s, s.repoSel); return true;
        case 'O': s.pendingBrowse = true; return true;
        case DEL: case 'X':
            if (n) s.pendingRemove = s.repoRows[s.repoSel].id;
            return true;
        case ESC:
            if (s.root.empty()) return false;  // nothing to go back to
            s.screen = HomeScreen::Home;
            s.repoNotice.clear();
            return true;
        default: return false;
    }
}

bool homeKey(HomeState& s, unsigned vk, bool ctrl, bool shift) {
    if (s.screen == HomeScreen::Picker) return pickerKey(s, vk, ctrl);
    enum : unsigned { BACK = 0x08, RET = 0x0D, ESC = 0x1B, PGUP = 0x21, PGDN = 0x22,
                      END = 0x23, HOME = 0x24, LEFT = 0x25, UP = 0x26, RIGHT = 0x27,
                      DOWN = 0x28, PERIOD = 0xBE, SLASH = 0xBF };
    if (ctrl) return false;  // Ctrl chords stay with the app

    if (s.searching) {
        switch (vk) {
            case ESC: s.query.clear(); s.searching = false; refilter(s); break;
            case RET: s.searching = false; break;  // keep the filter, back to navigating
            case BACK:
                if (s.query.empty()) s.searching = false;
                else { s.query.pop_back(); refilter(s); }
                break;
            case UP: homeMove(s, -1); break;
            case DOWN: homeMove(s, 1); break;
            default: break;  // letters arrive through homeChar
        }
        return true;  // typing must never fire app shortcuts
    }

    switch (vk) {
        case DOWN: case 'J': homeMove(s, 1); return true;
        case UP: case 'K': homeMove(s, -1); return true;
        case RIGHT: case 'L': case RET: homeActivate(s); return true;
        case LEFT: case 'H': case BACK: homeUp(s); return true;
        case HOME: homeMoveTo(s, 0); return true;
        case END: homeMoveTo(s, (int)s.view.size() - 1); return true;
        case 'G': homeMoveTo(s, shift ? (int)s.view.size() - 1 : 0); return true;
        case PGDN: homeMove(s, std::max(1, s.visibleRows - 1)); return true;
        case PGUP: homeMove(s, -std::max(1, s.visibleRows - 1)); return true;
        case SLASH:
            if (shift) return false;
            s.searching = true;
            s.swallow = L'/';
            return true;
        case 'O':  // switch repository
            s.screen = HomeScreen::Picker;
            s.refreshRows = true;
            s.repoNotice.clear();
            return true;
        case PERIOD: s.opts.showHidden = !s.opts.showHidden; rebuildView(s, L""); return true;
        case 'S': {
            s.opts.key = s.opts.key == SortKey::Name ? SortKey::Modified
                         : s.opts.key == SortKey::Modified ? SortKey::Size : SortKey::Name;
            s.opts.descending = s.opts.key != SortKey::Name;
            std::wstring keep = homeSelected(s) ? homeSelected(s)->name : L"";
            sortEntries(s.current.entries, s.opts);
            rebuildView(s, keep);
            return true;
        }
        case ESC:
            if (s.query.empty()) return false;
            s.query.clear();
            refilter(s);
            return true;
        default: return false;
    }
}

bool homeChar(HomeState& s, wchar_t c) {
    if (!s.searching) return false;
    if (s.swallow && c == s.swallow) { s.swallow = 0; return true; }
    s.swallow = 0;
    if (c < 32 || c == 127) return true;
    s.query.push_back(c);
    refilter(s);
    return true;
}

unsigned homeTakeRequests(HomeState& s) {
    unsigned n = s.need;
    s.need = 0;
    return n;
}

void homeSetRepoRows(HomeState& s, std::vector<RepoRow>&& rows) {
    s.repoRows = std::move(rows);
    int n = (int)s.repoRows.size();
    s.repoSel = n == 0 ? 0 : std::clamp(s.repoSel, 0, n - 1);
}

void homeShowPicker(HomeState& s) {
    s.screen = HomeScreen::Picker;
    s.refreshRows = true;
    s.repoNotice.clear();
}

void homeChooseRepo(HomeState& s, int index) {
    if (index < 0 || index >= (int)s.repoRows.size()) return;
    const RepoRow& row = s.repoRows[index];
    if (row.missing) {
        s.repoNotice = L"Folder not found: " + row.path;
        return;
    }
    s.repoNotice.clear();
    s.pendingChoose = row.id;
}
