#include "repos.h"

#include <algorithm>

namespace repos {

static bool isSep(char c) { return c == '\\' || c == '/'; }

static std::string trimSeps(std::string p) {
    while (p.size() > 1 && isSep(p.back()) && !(p.size() == 3 && p[1] == ':')) p.pop_back();
    return p;
}

std::string normPath(const std::string& path) {
    std::string p = trimSeps(path);
    for (auto& c : p) {
        if (c == '/') c = '\\';
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');  // ASCII only: UTF-8 safe
    }
    return trimSeps(p);
}

static std::string leaf(const std::string& path) {
    std::string p = trimSeps(path);
    size_t cut = p.find_last_of("\\/");
    return cut == std::string::npos ? p : p.substr(cut + 1);
}

static std::string parentLeaf(const std::string& path) {
    std::string p = trimSeps(path);
    size_t cut = p.find_last_of("\\/");
    return cut == std::string::npos ? std::string() : leaf(p.substr(0, cut));
}

int indexOfId(const std::vector<Repo>& list, const std::string& id) {
    for (int i = 0; i < (int)list.size(); i++)
        if (list[i].id == id) return i;
    return -1;
}

int indexOfPath(const std::vector<Repo>& list, const std::string& path) {
    std::string key = normPath(path);
    for (int i = 0; i < (int)list.size(); i++)
        if (normPath(list[i].path) == key) return i;
    return -1;
}

static std::string nextId(const std::vector<Repo>& list) {
    long max = 0;
    for (const auto& r : list) {
        if (r.id.size() > 1 && r.id[0] == 'r') {
            try { max = std::max(max, std::stol(r.id.substr(1))); } catch (...) {}
        }
    }
    return "r" + std::to_string(max + 1);
}

static bool sameName(const std::string& a, const std::string& b) {
    return normPath(a) == normPath(b);
}

void touch(std::vector<Repo>& list, const std::string& id, unsigned long long now) {
    int i = indexOfId(list, id);
    if (i < 0) return;
    list[i].lastOpened = now;
    std::rotate(list.begin(), list.begin() + i, list.begin() + i + 1);
}

std::string add(std::vector<Repo>& list, const std::string& path, unsigned long long now) {
    int existing = indexOfPath(list, path);
    if (existing >= 0) {
        std::string id = list[existing].id;
        touch(list, id, now);
        return id;
    }
    Repo r;
    r.id = nextId(list);
    r.path = trimSeps(path);
    r.name = leaf(r.path);
    if (r.name.empty()) r.name = r.path;
    for (const auto& other : list) {  // two folders named "notes": tell them apart
        if (sameName(other.name, r.name)) {
            std::string up = parentLeaf(r.path);
            if (!up.empty()) r.name += " (" + up + ")";
            break;
        }
    }
    r.lastOpened = now;
    list.insert(list.begin(), r);
    if (list.size() > kMaxRepos) list.resize(kMaxRepos);
    return list.front().id;
}

bool remove(std::vector<Repo>& list, const std::string& id) {
    int i = indexOfId(list, id);
    if (i < 0) return false;
    list.erase(list.begin() + i);
    return true;
}

std::string encode(const Repo& r) {
    std::string name = r.name;
    std::replace(name.begin(), name.end(), '|', '/');
    return r.id + "|" + std::to_string(r.lastOpened) + "|" + name + "|" + r.path + "|" + r.lastDir;
}

bool decode(const std::string& line, Repo& out) {
    std::vector<std::string> f;
    size_t start = 0;
    for (int i = 0; i < 4; i++) {
        size_t bar = line.find('|', start);
        if (bar == std::string::npos) return false;
        f.push_back(line.substr(start, bar - start));
        start = bar + 1;
    }
    f.push_back(line.substr(start));
    if (f[0].empty() || f[3].empty()) return false;
    Repo r;
    r.id = f[0];
    try { r.lastOpened = f[1].empty() ? 0 : std::stoull(f[1]); } catch (...) { return false; }
    r.name = f[2].empty() ? leaf(f[3]) : f[2];
    r.path = f[3];
    r.lastDir = f[4];
    out = std::move(r);
    return true;
}

Startup decideStartup(const std::vector<Repo>& list, const std::string& currentId,
                      int mode, const std::function<bool(const std::string&)>& dirExists) {
    Startup s;
    if (list.empty() || mode == kStartPicker) return s;
    int i = indexOfId(list, currentId);
    if (i < 0 || !dirExists(list[i].path)) return s;
    s.openRepo = true;
    s.id = list[i].id;
    return s;
}

}  // namespace repos
