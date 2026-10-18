#pragma once
// Repository list for the Home view: pure data rules, no Win32. Strings are
// UTF-8 like the rest of Settings. The list is most-recently-opened first.
#include <functional>
#include <string>
#include <vector>

namespace repos {

struct Repo {
    std::string id;    // "r1", "r2"...: stable key that survives a moved folder
    std::string name;  // display name, defaults to the folder's name
    std::string path;
    unsigned long long lastOpened = 0;  // FILETIME ticks
    std::string lastDir;                // last folder shown inside the repo
};

enum StartupMode { kStartLast = 0, kStartPicker = 1 };
constexpr size_t kMaxRepos = 32;

std::string normPath(const std::string& path);  // case/separator-insensitive key
int indexOfId(const std::vector<Repo>& list, const std::string& id);
int indexOfPath(const std::vector<Repo>& list, const std::string& path);

// Adds a repo (or re-uses the one with the same path) and moves it to the
// front. Returns its id.
std::string add(std::vector<Repo>& list, const std::string& path,
                unsigned long long now);
void touch(std::vector<Repo>& list, const std::string& id, unsigned long long now);
bool remove(std::vector<Repo>& list, const std::string& id);

// settings.ini line: id|when|name|path|lastDir  (Windows paths never contain '|')
std::string encode(const Repo& repo);
bool decode(const std::string& line, Repo& out);

struct Startup {
    bool openRepo = false;  // false = show the picker
    std::string id;
};
// Picker when: no repos, the user chose it, the last repo is unknown, or its
// folder is gone. Otherwise reopen the last repo.
Startup decideStartup(const std::vector<Repo>& list, const std::string& currentId,
                      int mode, const std::function<bool(const std::string&)>& dirExists);

}  // namespace repos
