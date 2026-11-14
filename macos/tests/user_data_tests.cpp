#include "user_data.hpp"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

void require(bool ok, const char* what)
{
    if (!ok) {
        throw std::runtime_error(what);
    }
}

std::string read(const fs::path& path)
{
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), {}};
}

} // namespace

int main()
{
    std::string pattern = (fs::temp_directory_path() / "nox-user-data-XXXXXX").string();
    const char* temp = mkdtemp(pattern.data());
    if (!temp) {
        return 1;
    }
    const fs::path root = temp, assets = root / "assets", user = root / "user";
    int result = 0;
    try {
        fs::create_directories(assets / "SAVE/Slot1");
        fs::create_directories(user / "Save/Slot1");
        std::ofstream(assets / "DEFAULT.CFG") << "VideoMode = 800 600 16\n";
        std::ofstream(assets / "noxcore.cfg") << "OutputScale = 350\n";
        std::ofstream(assets / "SAVE/new.plr") << "old character";
        std::ofstream(assets / "SAVE/Slot1/Player.plr") << "old progress";
        std::ofstream(user / "Save/Slot1/Player.plr") << "new progress";
        fs::permissions(assets / "noxcore.cfg", fs::perms::owner_read);
        fs::permissions(assets, fs::perms::owner_read | fs::perms::owner_exec);
        noxsdl::prepareUserData(assets, user, true);
        require(read(user / "default.cfg").find("VideoMode") != std::string::npos, "case-insensitive settings import");
        require(read(user / "Save/new.plr") == "old character", "character import");
        require(read(user / "Save/Slot1/Player.plr") == "new progress", "must preserve existing progress");
        std::ofstream(user / "noxcore.cfg") << "OutputScale = 200\n";
        require(read(user / "noxcore.cfg").find("200") != std::string::npos, "imported settings must be writable");
        fs::remove(user / "Save/new.plr");
        noxsdl::prepareUserData(assets, user, true);
        require(!fs::exists(user / "Save/new.plr"), "must not resurrect deleted character");
        require(read(user / "noxcore.cfg").find("200") != std::string::npos, "must preserve newer settings");
        noxsdl::prepareUserData(assets, root / "override", false);
        require(!fs::exists(root / "override/Save/new.plr"), "explicit save override skips migration");
        noxsdl::prepareUserData(assets, root / "override", true);
        require(read(root / "override/Save/new.plr") == "old character", "later default saves still import");
        require(read(assets / "SAVE/new.plr") == "old character", "source saves unchanged");
        require(read(assets / "noxcore.cfg") == "OutputScale = 350\n", "source settings unchanged");
        require(read(assets / "SAVE/Slot1/Player.plr") == "old progress", "source progress unchanged");
        for (const char* marker : {".settings-import-complete", ".saves-import-complete"}) {
            require(fs::is_regular_file(user / marker), "import marker belongs in user directory");
            require(!fs::exists(assets / marker), "no import markers in source assets");
        }

        // Overlapping save imports must fail before creating a tree inside their own source.
        for (const auto& nested : {assets / "SAVE/nested-user", assets / "save/case-alias-user"}) {
            if (!fs::exists(nested.parent_path())) continue; // Case aliases only on a case-insensitive volume.
            bool rejected = false;
            try { noxsdl::prepareUserData(assets, nested, true); }
            catch (const std::runtime_error&) { rejected = true; }
            require(rejected && !fs::exists(nested), "nested save import rejected without copying");
        }
        fs::create_directory_symlink(assets / "SAVE", root / "save-link");
        bool rejected = false;
        try { noxsdl::prepareUserData(assets, root / "save-link/nested-user", true); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected && !fs::exists(assets / "SAVE/nested-user"), "save overlap through symlink rejected");

        const auto bundled = root / "bundle-mods", mods = root / "mods";
        noxsdl::prepareUserMods(bundled, mods);
        require(!fs::exists(mods / ".bundled-import-complete"), "missing bundle must not consume import");
        fs::create_directories(bundled / "sample/server");
        fs::create_directories(bundled / "existing/server");
        fs::create_directories(mods / "existing");
        std::ofstream(bundled / "sample/server/main.luau") << "bundled script";
        std::ofstream(bundled / "existing/server/main.luau") << "unrelated bundled script";
        std::ofstream(mods / "existing/mode.json") << "user project";
        fs::permissions(bundled / "sample/server", fs::perms::owner_read | fs::perms::owner_exec);
        fs::permissions(bundled / "sample/server/main.luau", fs::perms::owner_read);
        noxsdl::prepareUserMods(bundled, mods);
        require(read(mods / "sample/server/main.luau") == "bundled script", "bundled mod imported");
        require(!fs::exists(mods / "existing/server"), "existing user project never merged with bundled one");
        std::ofstream(mods / "sample/server/main.luau") << "user edit";
        std::ofstream(mods / "sample/server/new.luau") << "new user file";
        require(read(mods / "sample/server/main.luau") == "user edit" && fs::exists(mods / "sample/server/new.luau"),
                "imported files and directories are writable");
        noxsdl::prepareUserMods(bundled, mods);
        require(read(mods / "sample/server/main.luau") == "user edit", "mod edits survive relaunch");
        fs::remove_all(mods / "sample");
        noxsdl::prepareUserMods(bundled, mods);
        require(!fs::exists(mods / "sample"), "deleted mod is not resurrected");
        fs::create_directories(bundled / "reference");
        std::ofstream(bundled / "reference/mode.json") << R"({"reference":true})";
        std::ofstream(bundled / "reference/script.luau") << "version one";
        std::ofstream(bundled / "reference/retired.luau") << "old file";
        noxsdl::prepareUserMods(bundled, mods);
        require(read(mods / "reference/script.luau") == "version one", "new bundled reference imported after first launch");
        const auto stamp = fs::last_write_time(mods / "reference/script.luau");
        noxsdl::prepareUserMods(bundled, mods);
        require(fs::last_write_time(mods / "reference/script.luau") == stamp, "unchanged references are not recopied");
        std::ofstream(bundled / "reference/script.luau") << "version two";
        fs::remove(bundled / "reference/retired.luau");
        noxsdl::prepareUserMods(bundled, mods);
        require(read(mods / "reference/script.luau") == "version two" && !fs::exists(mods / "reference/retired.luau"),
                "reference upgrades replace the complete tree without stale files");
        std::ofstream(mods / "reference/mode.json") << R"({"reference":false})";
        std::ofstream(mods / "reference/script.luau") << "editable user version";
        noxsdl::prepareUserMods(bundled, mods);
        require(read(mods / "reference/script.luau") == "editable user version", "editable project is never replaced by a reference");
        fs::permissions(bundled / "sample/server", fs::perms::owner_all);
        std::ofstream(root / "not-directory") << "blocked";
        bool failed = false;
        try {
            noxsdl::prepareUserData(assets, root / "not-directory/state", true);
        } catch (const fs::filesystem_error&) {
            failed = true;
        }
        require(failed, "invalid destination must report failure");
        std::cout << "PASS: read-only assets, save import, no overwrite/resurrection, writable config, override, failure\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    fs::permissions(assets, fs::perms::owner_all);
    fs::remove_all(root);
    return result;
}
