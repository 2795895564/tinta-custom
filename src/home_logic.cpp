#include "home_state.h"

#include <algorithm>
#include <cwctype>

using namespace dirmodel;

HomeColumnsLayout homeColumnsLayout(float viewWidth, int count, float pad, float gap) {
    HomeColumnsLayout out;
    if (count < 1) count = 1;
    const float avail = std::max(0.0f, viewWidth - 2.0f * pad);
    // Single columns get room to breathe on wide windows (800 DIP, 58% of
    // the window, never wider than 900); two and three columns keep the
    // classic Miller rhythm. Every width is capped by the space that is
    // actually there, so narrow windows fill edge to edge.
    const float preferred =
        count == 1 ? std::clamp(viewWidth * 0.58f, 800.0f, 900.0f)
        : count == 2 ? 520.0f
                     : 400.0f;
    const float fit = (avail - (count - 1) * gap) / (float)count;
    out.width = std::max(1.0f, std::min(preferred, fit));
    out.totalWidth = count * out.width + (count - 1) * gap;
    out.startX = (viewWidth - out.totalWidth) / 2.0f;
    return out;
}

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

bool validHomeName(const std::wstring& name) {
    if (name.empty() || name.size() > 255) return false;
    if (name == L"." || name == L"..") return false;
    for (wchar_t c : name) {
        // Reject C0/C1 controls and invisible format characters that would
        // make a visually valid-looking name fail in CreateFileW. CJK and
        // other printable scripts must stay allowed.
        if (c < 32 || c == 127 || (c >= 0x80 && c <= 0x9F)) return false;
        if ((c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) ||
            (c >= 0x2060 && c <= 0x206F) || c == 0xFEFF || c == 0x00AD) {
            return false;
        }
        if (c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' ||
            c == L'\\' || c == L'|' || c == L'?' || c == L'*') {
            return false;
        }
    }
    if (name.back() == L' ' || name.back() == L'.') return false;
    std::wstring base = name.substr(0, name.find(L'.'));
    for (auto& c : base) c = (wchar_t)std::towupper(c);
    static const wchar_t* reserved[] = {
        L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3", L"COM4",
        L"COM5", L"COM6", L"COM7", L"COM8", L"COM9", L"LPT1", L"LPT2",
        L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"};
    for (const wchar_t* word : reserved)
        if (base == word) return false;
    return true;
}

void beginDialog(HomeState& s, HomeDialogKind kind) {
    s.dialog = kind;
    s.dialogCommit = false;
    s.dialogError = HomeDialogError::None;
    s.dialogErrorCode = 0;
    s.dialogText.clear();
    s.dialogOriginal.clear();
    s.dialogCaret = 0;
    s.dialogParent = s.dir;
    if (kind == HomeDialogKind::Rename || kind == HomeDialogKind::ConfirmDelete) {
        const Entry* e = homeSelected(s);
        if (!e) { s.dialog = HomeDialogKind::None; return; }
        s.dialogOriginal = e->name;
        if (kind == HomeDialogKind::Rename) {
            s.dialogText = e->name;
            s.dialogCaret = (int)e->name.size();
        }
    }
}

bool dialogKey(HomeState& s, unsigned vk) {
    if (s.dialog == HomeDialogKind::ConfirmDelete) {
        if (vk == 0x0D || vk == 'Y') { s.dialogCommit = true; return true; }
        if (vk == 0x1B || vk == 'N') { homeDialogClose(s); return true; }
        return true;  // modal: swallow everything else
    }
    if (vk == 0x1B) { homeDialogClose(s); return true; }
    if (vk == 0x0D) {
        if (s.dialogText.empty()) {
            s.dialogError = HomeDialogError::Empty;
        } else if (!validHomeName(s.dialogText)) {
            s.dialogError = HomeDialogError::Invalid;
        } else if (s.dialog == HomeDialogKind::Rename &&
                   s.dialogText == s.dialogOriginal) {
            homeDialogClose(s);  // unchanged: no-op
        } else {
            s.dialogError = HomeDialogError::None;
            s.dialogCommit = true;
        }
        return true;
    }
    const int length = (int)s.dialogText.size();
    switch (vk) {
        case 0x08:  // Backspace
            if (s.dialogCaret > 0) {
                s.dialogText.erase(s.dialogCaret - 1, 1);
                s.dialogCaret--;
                s.dialogError = HomeDialogError::None;
            }
            return true;
        case 0x2E:  // Delete
            if (s.dialogCaret < length) {
                s.dialogText.erase(s.dialogCaret, 1);
                s.dialogError = HomeDialogError::None;
            }
            return true;
        case 0x25:  // Left
            if (s.dialogCaret > 0) s.dialogCaret--;
            return true;
        case 0x27:  // Right
            if (s.dialogCaret < length) s.dialogCaret++;
            return true;
        case 0x24:  // Home
            s.dialogCaret = 0;
            return true;
        case 0x23:  // End
            s.dialogCaret = length;
            return true;
        default:
            return true;  // modal: swallow navigation keys too
    }
}

}  // namespace

bool homeHasRoot(const HomeState& s) { return !s.root.empty(); }

void homeDialogClose(HomeState& s) {
    s.dialog = HomeDialogKind::None;
    s.dialogText.clear();
    s.dialogOriginal.clear();
    s.dialogParent.clear();
    s.dialogCaret = 0;
    s.dialogError = HomeDialogError::None;
    s.dialogErrorCode = 0;
    s.dialogCommit = false;
    s.dialogFieldHit = HomeRect{};
    s.dialogPrimaryHit = HomeRect{};
    s.dialogCancelHit = HomeRect{};
    s.dialogCaretX = 0;
    s.dialogCaretY = 0;
}

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
    homeDialogClose(s);  // any explicit navigation leaves the modal
    s.dir = dir;
    s.fileNotice.clear();
    s.query.clear();
    s.searching = false;
    s.restoreName = select;
    s.current = Listing{};
    s.view.clear();
    s.selected.clear();  // selection belongs to the directory being left
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
        // A rescan may have removed entries; drop stale selected names.
        for (auto it = s.selected.begin(); it != s.selected.end();) {
            bool found = false;
            for (const auto& e : s.current.entries)
                if (lower(e.name) == *it) { found = true; break; }
            if (!found) it = s.selected.erase(it);
            else ++it;
        }
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

size_t homeSelectionCount(const HomeState& s) { return s.selected.size(); }

bool homeIsSelected(const HomeState& s, const std::wstring& name) {
    return s.selected.find(lower(name)) != s.selected.end();
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
    if (vk != DEL && vk != 'X' && !s.confirmRemove.empty()) {
        s.confirmRemove.clear();
        s.repoNotice.clear();
    }
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
        case 'R':
            if (n && s.repoRows[s.repoSel].missing) {
                s.pendingRelocate = s.repoRows[s.repoSel].id;
                s.repoNotice.clear();
            } else if (n) {
                s.repoNotice =
                    L"Select a missing repository (marked \"folder missing\") to relocate it.";
            }
            return true;
        case 'D':
            if (n && !s.repoRows[s.repoSel].missing) {
                s.pendingDefault = s.repoRows[s.repoSel].id;
                s.repoNotice.clear();
            } else if (n) {
                s.repoNotice = L"A missing repository cannot be the startup repository.";
            }
            return true;
        case DEL: case 'X':
            if (n) {
                const std::string id = s.repoRows[s.repoSel].id;
                if (s.confirmRemove == id) {
                    s.pendingRemove = id;
                    s.confirmRemove.clear();
                    s.repoNotice.clear();
                } else {
                    s.confirmRemove = id;
                    s.repoNotice =
                        L"Press X again to forget this repository (the folder stays on disk).";
                }
            }
            return true;
        case ESC:
            // The picker is the bottom of the Esc stack when no repository is
            // open: consume it and stay put. Esc never quits the application.
            if (s.root.empty()) return true;
            s.screen = HomeScreen::Home;
            s.repoNotice.clear();
            return true;
        default: return false;
    }
}

bool homeKey(HomeState& s, unsigned vk, bool ctrl, bool shift) {
    if (s.dialog != HomeDialogKind::None) return dialogKey(s, vk);
    if (s.screen == HomeScreen::Picker) return pickerKey(s, vk, ctrl);
    enum : unsigned { BACK = 0x08, RET = 0x0D, ESC = 0x1B, PGUP = 0x21, PGDN = 0x22,
                      END = 0x23, HOME = 0x24, LEFT = 0x25, UP = 0x26, RIGHT = 0x27,
                      DOWN = 0x28, DEL = 0x2E, PERIOD = 0xBE, SLASH = 0xBF,
                      F2 = 0x71 };
    if (ctrl && vk == 'A' && !s.searching) {
        s.selected.clear();
        for (int idx : s.view) {
            if (idx >= 0 && idx < (int)s.current.entries.size())
                s.selected.insert(lower(s.current.entries[idx].name));
        }
        return true;
    }
    if (ctrl && (vk == 'C' || vk == 'X') && !s.searching) {
        s.clipboard.clear();
        if (!s.selected.empty()) {
            for (int idx : s.view) {
                if (idx < 0 || idx >= (int)s.current.entries.size()) continue;
                const Entry& e = s.current.entries[idx];
                if (s.selected.find(lower(e.name)) != s.selected.end())
                    s.clipboard.push_back(join(s.dir, e.name));
            }
        } else if (const Entry* e = homeSelected(s)) {
            s.clipboard.push_back(join(s.dir, e->name));
        }
        s.clipboardCut = (vk == 'X');
        return true;
    }
    if (ctrl && vk == 'V' && !s.searching) {
        if (!s.clipboard.empty()) s.pendingPaste = true;
        return true;
    }
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
        case 0x20:  // Space toggles the row under the cursor
            if (const Entry* e = homeSelected(s)) {
                const std::wstring key = lower(e->name);
                auto it = s.selected.find(key);
                if (it == s.selected.end()) s.selected.insert(key);
                else s.selected.erase(it);
            }
            return true;
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
            s.selected.clear();
            s.lastClickRow = -1;
            s.lastClickTick = 0;
            return true;
        case 'N':
            beginDialog(s, shift ? HomeDialogKind::NewFolder
                                 : HomeDialogKind::NewFile);
            return true;
        case 'R':
        case F2:
            beginDialog(s, HomeDialogKind::Rename);
            return true;
        case 'D':
        case DEL:
            beginDialog(s, HomeDialogKind::ConfirmDelete);
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
            // Always consume: selection first, then the filter, then a no-op
            // (Esc never quits).
            if (!s.selected.empty()) { s.selected.clear(); return true; }
            if (s.query.empty()) return true;
            s.query.clear();
            refilter(s);
            return true;
        default: return false;
    }
}

bool homeChar(HomeState& s, wchar_t c) {
    if (s.dialog != HomeDialogKind::None) {
        if (s.dialog == HomeDialogKind::ConfirmDelete) return true;
        if (c >= 32) {
            int at = std::clamp(s.dialogCaret, 0, (int)s.dialogText.size());
            s.dialogText.insert(s.dialogText.begin() + at, c);
            s.dialogCaret = at + 1;
            s.dialogError = HomeDialogError::None;
        }
        return true;
    }
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
    s.confirmRemove.clear();
    s.selected.clear();
    s.lastClickRow = -1;  // do not pair a picker click with an old file click
    s.lastClickTick = 0;
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
