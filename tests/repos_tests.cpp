#include <cstdio>
#include "repos.h"
using namespace repos;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    std::vector<Repo> L;
    std::string a = add(L, "C:\\notes", 10);
    std::string b = add(L, "D:\\work\\docs\\", 20);
    CHECK(a == "r1" && b == "r2" && L.size() == 2);
    CHECK(L[0].id == "r2" && L[0].name == "docs" && L[0].path == "D:\\work\\docs");  // newest first, trailing \ trimmed

    // same folder again (case, slash style) re-uses the entry and moves it up
    CHECK(add(L, "c:/Notes/", 30) == "r1" && L.size() == 2 && L[0].id == "r1" && L[0].lastOpened == 30);

    // two folders with the same name get told apart
    std::string c = add(L, "E:\\school\\notes", 40);
    CHECK(L[0].id == c && L[0].name == "notes (school)" && L[1].name == "notes");

    // ids never repeat after a removal
    CHECK(remove(L, "r2") && !remove(L, "r2"));
    CHECK(add(L, "F:\\x", 50) == "r4");

    // roundtrip, including a '|' in the name and non-ASCII
    Repo r; r.id = "r9"; r.name = "a|b 笔记"; r.path = "D:\\笔记"; r.lastOpened = 123456789012345ULL; r.lastDir = "D:\\笔记\\2026";
    Repo back;
    CHECK(decode(encode(r), back) && back.id == "r9" && back.name == "a/b 笔记" && back.path == r.path && back.lastOpened == r.lastOpened && back.lastDir == r.lastDir);
    CHECK(!decode("r1|12|name", back) && !decode("|1|n|C:\\x|", back) && !decode("r1|zz|n|C:\\x|", back));
    CHECK(decode("r1|0||C:\\x\\y|", back) && back.name == "y" && back.lastDir.empty());

    // startup decision
    auto exists = [](const std::string& p) { return p != "D:\\gone"; };
    std::vector<Repo> S; add(S, "C:\\notes", 1); add(S, "D:\\gone", 2);
    CHECK(!decideStartup({}, "r1", kStartLast, exists).openRepo);
    CHECK(decideStartup(S, "r1", kStartLast, exists).openRepo && decideStartup(S, "r1", kStartLast, exists).id == "r1");
    CHECK(!decideStartup(S, "r1", kStartPicker, exists).openRepo);  // user wants the picker
    CHECK(!decideStartup(S, "r2", kStartLast, exists).openRepo);    // folder gone -> picker
    CHECK(!decideStartup(S, "zz", kStartLast, exists).openRepo);    // unknown id -> picker

    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
